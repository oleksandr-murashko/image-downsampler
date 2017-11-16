#include <QCoreApplication>
#include <QStringList>
#include <QTextStream>
#include <QImage>
#include <QDateTime>

#include "immintrin.h"

typedef __m128i int32x4;
typedef __m256d fp64x4;

int main(int argc, char *argv[])
{
    const unsigned long long startTime = QDateTime::currentMSecsSinceEpoch();

    const QStringList arguments = QCoreApplication(argc, argv).arguments();

    if (arguments.length() != 5) {
        QTextStream(stderr, QIODevice::WriteOnly) << "[image-downsampler] error: incorrect number of arguments; use syntax: image-downsampler <input_image> <output_image> <target_width> <target_height>\n";
        return -1;
    }

    // ---------------------------------------------------------------------------------------------- //
    // -- open input image -------------------------------------------------------------------------- //
    // ---------------------------------------------------------------------------------------------- //

    QImage originalImage(arguments[1]);
    if (originalImage.isNull()) {
        QTextStream(stderr, QIODevice::WriteOnly) << "[image-downsampler] error: can't open original image.\n";
        return -2;
    }

    const int originalWidth = originalImage.width();
    const int originalHeight = originalImage.height();

    const QString outputFileName = arguments[2];
    const int targetWidth = arguments[3].toInt();
    const int targetHeight = arguments[4].toInt();

    if ((targetWidth > originalWidth) || (targetHeight > originalHeight)) {
        QTextStream(stderr, QIODevice::WriteOnly) << "[image-downsampler] error: please specify smaller image dimensions for resizing.\n";
        return -3;
    }

    // ---------------------------------------------------------------------------------------------- //
    // -- extract image data (RGB colour channels) -------------------------------------------------- //
    // ---------------------------------------------------------------------------------------------- //

    if (originalImage.format() != QImage::Format_RGB32) originalImage = originalImage.convertToFormat(QImage::Format_RGB32);
    const unsigned int *imageData = (const unsigned int *) originalImage.constBits();

    // ---------------------------------------------------------------------------------------------- //
    // -- integration in the horizontal direction by averaging pixels in each row ------------------- //
    // ---------------------------------------------------------------------------------------------- //

    // create an array for image rescaled by width only
    const int partlyResizedImageArea = targetWidth * originalHeight;
    fp64x4 *partlyResized = (fp64x4 *) _mm_malloc(partlyResizedImageArea * sizeof(fp64x4), sizeof(fp64x4));

    // current row number
    unsigned int *currentRow = (unsigned int *) imageData;

    // position counter indicating where to save a partial sum to the output array
    int resultPosition = 0;

    for (int old_y = 0;  old_y < originalHeight;  old_y++)
    {
        // no need to explicitly calculate left border - it's always starts at the left edge
        double leftPixelBorderCoordinate = 0;

        for (int new_x = 0;  new_x < targetWidth;  new_x++)
        {
            // calculate right pixel border coodrinate using multiplication and division to avoid loss of precision
            const double rightPixelBorderCoordinate = (new_x + 1) * originalWidth / (double) targetWidth;

            // left pixel fraction
            const int leftColumn = (int) leftPixelBorderCoordinate;
            const double leftPercent = 1.0 - (leftPixelBorderCoordinate - leftColumn);
            register fp64x4 sum = _mm256_cvtepi32_pd(_mm_cvtepu8_epi32(_mm_set1_epi32(currentRow[leftColumn]))) * leftPercent;  //_mm256_set1_pd(leftPercent);

            // right pixel fraction
            const int rightColumn = (int) rightPixelBorderCoordinate;
            const double rightPercent = rightPixelBorderCoordinate - rightColumn;
            if (rightPercent != 0)  // if percent = 0 column may not exist
            {
                sum += _mm256_cvtepi32_pd(_mm_cvtepu8_epi32(_mm_set1_epi32(currentRow[rightColumn]))) * rightPercent;  //_mm256_set1_pd(rightPercent);
            }

            // any remaining pixels in between
            for (int i = leftColumn + 1;  i < rightColumn;  i++)
            {
                sum += _mm256_cvtepi32_pd(_mm_cvtepu8_epi32(_mm_set1_epi32(currentRow[i])));
            }

            // save sums
            partlyResized[resultPosition] = sum;
            resultPosition++;

            // move left border coordinate for the next iteration
            leftPixelBorderCoordinate = rightPixelBorderCoordinate;
        }

        // change current row coordinate for the next iteration
        currentRow += originalWidth;
    }

    // ---------------------------------------------------------------------------------------------- //
    // -- integration in the vertical direction by averaging pixels in each column ------------------ //
    // ---------------------------------------------------------------------------------------------- //

    // create an empty image to fill pixel by pixel
    QImage resizedImage (targetWidth, targetHeight, QImage::Format_RGB32);
    unsigned int *resizedImageData = (unsigned int *) resizedImage.constBits();
    int pixelCounter = 0;

    // calculate 2D scale coefficient (or normalization coefficient)
    const double normalization = originalWidth * originalHeight / (double) (targetWidth * targetHeight);

    // first edge is at the top of the image
    double topPixelBorderCoordinate = 0;

    for (int new_y = 0;  new_y < targetHeight;  new_y++)
    {
        // calculate bottom border coodrinate using multiplication and division to avoid loss of precision
        const double bottomPixelBorderCoordinate = (new_y + 1) * originalHeight / (double) targetHeight;

        // top pixel fraction
        const int topRow = (int) topPixelBorderCoordinate;
        const double topPercent = 1.0 - (topPixelBorderCoordinate - topRow);
        const fp64x4 topPercent_fp64x4 = _mm256_set1_pd(topPercent);

        // bottom pixel fraction

        const int bottomRow = (int) bottomPixelBorderCoordinate;
        const double bottomPercent = bottomPixelBorderCoordinate - bottomRow;
        const fp64x4 bottomPercent_fp64x4 = _mm256_set1_pd(bottomPercent);

        // summation loop across columns
        int currentTopRowCoordinate = topRow * targetWidth;        // Counters that facilitate navigation across
        int currentBottomRowCoordinate = bottomRow * targetWidth;  // top and bottom rows crossed by pixel borders

        for (int new_x = 0;  new_x < targetWidth;  new_x++)
        {
            register fp64x4 sum = partlyResized[currentTopRowCoordinate] * topPercent_fp64x4;
            currentTopRowCoordinate++;

            if (bottomPercent != 0)
            {
                sum += partlyResized[currentBottomRowCoordinate] * bottomPercent_fp64x4;
                currentBottomRowCoordinate++;
            }

            // any remaining pixels in between
            for (int i = topRow + 1;  i < bottomRow;  i++)
            {
                const int partialPixelCoordinate = i * targetWidth + new_x;
                sum += partlyResized[partialPixelCoordinate];
            }

            // calculate final pixel values;
            // due to rounding flags in MXCSR register we have to make complicated roundings (by default 2.5 -> 2.0, 255.5 -> 256.0)
            const fp64x4 pixel_fp64x4 = _mm256_div_pd(sum, _mm256_set1_pd(normalization));
            const int32x4 pixel_int32x4 = _mm256_cvtpd_epi32(_mm256_round_pd(pixel_fp64x4 + 0.5, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC));

            const unsigned int pixel = 0xff000000 | (_mm_extract_epi32(pixel_int32x4, 2) << 16) | (_mm_extract_epi32(pixel_int32x4, 1) << 8) | _mm_extract_epi32(pixel_int32x4, 0);
            resizedImageData[pixelCounter++] = pixel;
        }

        // move top border coordinate for the next iteration
        topPixelBorderCoordinate = bottomPixelBorderCoordinate;
    }

    // delete temporary arrays
    _mm_free(partlyResized);

    // ---------------------------------------------------------------------------------------------- //
    // -- save resized image ------------------------------------------------------------------------ //
    // ---------------------------------------------------------------------------------------------- //

    if (resizedImage.save(outputFileName))
    {
        const unsigned long long elapsedTime = QDateTime::currentMSecsSinceEpoch() - startTime;
        QTextStream cout(stdout, QIODevice::WriteOnly);
        cout << "[image-downsampler] " << originalWidth << 'x' << originalHeight << " -> " << targetWidth << 'x' << targetHeight;
        cout << " (" << elapsedTime << " ms)" << '\n';
        return 0;
    }
    else
    {
        QTextStream(stderr, QIODevice::WriteOnly) << "[image-downsampler] error: can't save output image as " << outputFileName << '\n';
        return -4;
    }
}
