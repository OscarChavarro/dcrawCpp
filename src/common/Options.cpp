// C
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// App modules
#include "Options.h"

#ifdef LINUX_PLATFORM
    #define nullptr 0
#endif

Options *OPTIONS_values;

Options::Options() { // NOLINT(cppcoreguidelines-pro-type-member-init)
    user_flip = -1;
    timestamp_only = 0;
    thumbnail_only = 0;
    identify_only = 0;
    useFujiRotate = 1;
    readFromStdin = 0;
    bpfile = nullptr;
    dark_frame = nullptr;
    user_qual = -1;
    user_black = -1;
    user_sat = -1;
#ifndef NO_LCMS
    cameraIccProfileFilename = nullptr;
    customOutputProfileForColorSpace = nullptr;
#endif
    write_to_stdout = 0;
    shotSelect = 0;
    multiOut = 0;
    brightness = 1.0;
    threshold = 0.0;
    halfSizePreInterpolation = 0;
    fourColorRgb = 0;
    documentMode = 0;
    highlight = 0;
    verbose = 0;
    useAutoWb = 0;
    useCameraWb = 0;
    useCameraMatrix = 1;
    outputColorSpace = 1;
    outputBitsPerPixel = 8;
    outputTiff = 0;
    med_passes = 0;
    noAutoBright = 0;

    chromaticAberrationCorrection[0] = 1;
    chromaticAberrationCorrection[1] = 1;
    chromaticAberrationCorrection[2] = 1;
    chromaticAberrationCorrection[3] = 1;

    gammaParameters[0] = 0.45;
    gammaParameters[1] = 4.5;
    gammaParameters[2] = 0;
    gammaParameters[3] = 0;
    gammaParameters[4] = 0;
    gammaParameters[5] = 0;

    greyBox[0] = 0;
    greyBox[1] = 0;
    greyBox[2] = UINT_MAX;
    greyBox[3] = UINT_MAX;

    userMul[0] = 0;
    userMul[1] = 0;
    userMul[2] = 0;
    userMul[3] = 0;
}

Options::~Options() {
}

void
Options::printHelp(const char **argv) {
    printf("\nRaw photo decoder \"dcraw\" v%s", DCRAW_VERSION);
    printf("\nby Dave Coffin, dcoffin a cybercom o net\n");
    printf("\nUsage:  %s [OPTION]... [FILE]...\n\n", argv[0]);
    puts("-v        Print verbose messages");
    puts("-c        Write GLOBAL_image data to standard output");
    puts("-e        Extract embedded thumbnail GLOBAL_image");
    puts("-i        Identify files without decoding them");
    puts("-i -v     Identify files and show metadata");
    puts("-z        Change file dates to camera timestamp");
    puts("-w        Use camera white balance, if possible");
    puts("-a        Average the whole GLOBAL_image for white balance");
    puts("-A <x y w h> Average a grey box for white balance");
    puts("-r <r g b g> Set custom white balance");
    puts("+M/-M     Use/don't use an embedded color matrix");
    puts("-C <r b>  Correct chromatic aberration");
    puts("-P <file> Fix the dead pixels listed in this file");
    puts("-K <file> Subtract dark frame (16-bit raw PGM)");
    puts("-k <num>  Set the darkness level");
    puts("-S <num>  Set the saturation level");
    puts("-n <num>  Set threshold for wavelet denoising");
    puts("-H [0-9]  Highlight mode (0=clip, 1=unclip, 2=blend, 3+=rebuild)");
    puts("-t [0-7]  Flip GLOBAL_image (0=none, 3=180, 5=90CCW, 6=90CW)");
    puts("-o [0-6]  Output colorspace (raw,sRGB,Adobe,Wide,ProPhoto,XYZ,ACES)");
#ifndef NO_LCMS
    puts("-o <file> Apply output ICC profile from file");
    puts("-p <file> Apply camera ICC profile from file or \"embed\"");
#endif
    puts("-d        Document mode (no color, no interpolation)");
    puts("-D        Document mode without scaling (totally raw)");
    puts("-j        Don't stretch or rotate raw pixels");
    puts("-W        Don't automatically brighten the GLOBAL_image");
    puts("-b <num>  Adjust brightness (default = 1.0)");
    puts("-g <p ts> Set custom gamma curve (default = 2.222 4.5)");
    puts("-q [0-3]  Set the interpolation quality");
    puts("-h        Half-size color GLOBAL_image (twice as fast as \"-q 0\")");
    puts("-f        Interpolate RGGB as four colors");
    puts("-m <num>  Apply a 3x3 median filter to R-G and B-G");
    puts("-s [0..N-1] Select one raw GLOBAL_image or \"all\" from each file");
    puts("-6        Write 16-bit instead of 8-bit");
    puts("-4        Linear 16-bit, same as \"-6 -W -g 1 1\"");
    puts("-T        Write TIFF instead of PPM");
    puts("");
}

int
Options::setArguments(int argc, const char **argv) {
    int arg;
    char *cp;
    char opm;
    char opt;
    const char *sp;

    if ( argc == 1 ) {
        printHelp(argv);
        exit(1);
    }
    argv[argc] = "";

    for ( arg = 1; (((opm = argv[arg][0]) - 2) | 2) == '+'; ) {
        opt = argv[arg++][1];
        if ( (cp = (char *) strchr (sp="nbrkStqmHACg", opt)) ) {
            for ( int i = 0; i < "114111111422"[cp - sp] - '0'; i++ ) {
                if ( !isdigit(argv[arg + i][0])) {
                    fprintf(stderr, "Non-numeric argument to \"-%c\"\n", opt);
                    return 1;
                }
            }
        }

        switch ( opt ) {
            case 'n':
                threshold = atof(argv[arg++]);
                break;
            case 'b':
                brightness = atof(argv[arg++]);
                break;
            case 'r':
                for ( int c = 0; c < 4; c++ ) {
                    userMul[c] = atof(argv[arg++]);
                }
                break;
            case 'C':
                chromaticAberrationCorrection[0] = 1 / atof(argv[arg++]);
                chromaticAberrationCorrection[2] = 1 / atof(argv[arg++]);
                break;
            case 'g':
                gammaParameters[0] = atof(argv[arg++]);
                gammaParameters[1] = atof(argv[arg++]);
                if ( gammaParameters[0] ) {
                    gammaParameters[0] = 1 / gammaParameters[0];
                }
                break;
            case 'k':
                user_black = atoi(argv[arg++]);
                break;
            case 'S':
                user_sat = atoi(argv[arg++]);
                break;
            case 't':
                user_flip = atoi(argv[arg++]);
                break;
            case 'q':
                user_qual = atoi(argv[arg++]);
                break;
            case 'm':
                med_passes = atoi(argv[arg++]);
                break;
            case 'H':
                highlight = atoi(argv[arg++]);
                break;
            case 's':
                shotSelect = abs(atoi(argv[arg]));
                multiOut = !strcmp(argv[arg++], "all");
                break;
            case 'o':
                if ( isdigit(argv[arg][0]) && !argv[arg][1] ) {
                    outputColorSpace = atoi(argv[arg++]);
                }
            #ifndef NO_LCMS
                else {
                    customOutputProfileForColorSpace = argv[arg++];
                }
                break;
            case 'p':
                cameraIccProfileFilename = argv[arg++];
            #endif
                break;
            case 'P':
                bpfile = argv[arg++];
                break;
            case 'K':
                dark_frame = argv[arg++];
                break;
            case 'z':
                timestamp_only = 1;
                break;
            case 'e':
                thumbnail_only = 1;
                break;
            case 'i':
                identify_only = 1;
                break;
            case 'c':
                write_to_stdout = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'h':
                halfSizePreInterpolation = 1;
                break;
            case 'f':
                fourColorRgb = 1;
                break;
            case 'A':
                for ( int c = 0; c < 4; c++ ) {
                    greyBox[c] = atoi(argv[arg++]);
                }
            case 'a':
                useAutoWb = 1;
                break;
            case 'w':
                useCameraWb = 1;
                break;
            case 'M':
                useCameraMatrix = 3 * (opm == '+');
                break;
            case 'I':
                readFromStdin = 1;
                break;
            case 'E':
                documentMode++;
            case 'D':
                documentMode++;
            case 'd':
                documentMode++;
            case 'j':
                useFujiRotate = 0;
                break;
            case 'W':
                noAutoBright = 1;
                break;
            case 'T':
                outputTiff = 1;
                break;
            case '4':
                gammaParameters[0] = gammaParameters[1] = noAutoBright = 1;
            case '6':
                outputBitsPerPixel = 16;
                break;
            default:
                fprintf (stderr, "Unknown option \"-%c\".\n", opt);
                exit(1);
        }
    }

    if ( arg == argc ) {
        fprintf (stderr, "No files to process.\n");
        exit(1);
    }

    return arg;
}
