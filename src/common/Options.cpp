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
    puts("-c        Write image data to standard output");
    puts("-e        Extract embedded thumbnail image");
    puts("-i        Identify files without decoding them");
    puts("-i -v     Identify files and show metadata");
    puts("-z        Change file dates to camera timestamp");
    puts("-w        Use camera white balance, if possible");
    puts("-a        Average the whole image for white balance");
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
    puts("-t [0-7]  Flip image (0=none, 3=180, 5=90CCW, 6=90CW)");
    puts("-o [0-6]  Output colorspace (raw,sRGB,Adobe,Wide,ProPhoto,XYZ,ACES)");
#ifndef NO_LCMS
    puts("-o <file> Apply output ICC profile from file");
    puts("-p <file> Apply camera ICC profile from file or \"embed\"");
#endif
    puts("-d        Document mode (no color, no interpolation)");
    puts("-D        Document mode without scaling (totally raw)");
    puts("-j        Don't stretch or rotate raw pixels");
    puts("-W        Don't automatically brighten the image");
    puts("-b <num>  Adjust brightness (default = 1.0)");
    puts("-g <p ts> Set custom gamma curve (default = 2.222 4.5)");
    puts("-q [0-3]  Set the interpolation quality");
    puts("-h        Half-size color image (twice as fast as \"-q 0\")");
    puts("-f        Interpolate RGGB as four colors");
    puts("-m <num>  Apply a 3x3 median filter to R-G and B-G");
    puts("-s [0..N-1] Select one raw image or \"all\" from each file");
    puts("-6        Write 16-bit instead of 8-bit");
    puts("-4        Linear 16-bit, same as \"-6 -W -g 1 1\"");
    puts("-T        Write TIFF instead of PPM");
    puts("");
}

int
Options::setArguments(int argc, const char **argv) {
    int arg;
    int i;
    char *cp;
    char opm;
    char opt;
    const char *sp;

    if (argc == 1) {
        OPTIONS_values->printHelp(argv);
        exit(1);
    }
    argv[argc] = "";
    for ( arg = 1; (((opm = argv[arg][0]) - 2) | 2) == '+'; ) {
        opt = argv[arg++][1];
        if ( (cp = (char *) strchr (sp="nbrkStqmHACg", opt)) ) {
            for ( i = 0; i < "114111111422"[cp - sp] - '0'; i++ ) {
                if ( !isdigit(argv[arg + i][0])) {
                    fprintf(stderr, "Non-numeric argument to \"-%c\"\n", opt);
                    return 1;
                }
            }
        }

        char *end;
        switch (opt) {
            case 'n':
                OPTIONS_values->threshold = atof(argv[arg++]);
                break;
            case 'b':
                OPTIONS_values->brightness = atof(argv[arg++]);
                break;
            case 'r':
                for ( int c = 0; c < 4; c++ ) {
                    userMul[c] = atof(argv[arg++]);
                }
                break;
            case 'C':
                OPTIONS_values->chromaticAberrationCorrection[0] = 1 / atof(argv[arg++]);
                OPTIONS_values->chromaticAberrationCorrection[2] = 1 / atof(argv[arg++]);
                break;
            case 'g':
                OPTIONS_values->gammaParameters[0] = atof(argv[arg++]);
                OPTIONS_values->gammaParameters[1] = atof(argv[arg++]);
                if ( OPTIONS_values->gammaParameters[0] ) {
                    OPTIONS_values->gammaParameters[0] = 1 / OPTIONS_values->gammaParameters[0];
                }
                break;
            case 'k':
                OPTIONS_values->user_black = atoi(argv[arg++]);
                break;
            case 'S':
                OPTIONS_values->user_sat = atoi(argv[arg++]);
                break;
            case 't':
                OPTIONS_values->user_flip = atoi(argv[arg++]);
                break;
            case 'q':
                OPTIONS_values->user_qual = atoi(argv[arg++]);
                break;
            case 'm':
                OPTIONS_values->med_passes = atoi(argv[arg++]);
                break;
            case 'H':
                OPTIONS_values->highlight = atoi(argv[arg++]);
                break;
            case 's':
                OPTIONS_values->shotSelect = abs(atoi(argv[arg]));
                OPTIONS_values->multiOut = !strcmp(argv[arg++], "all");
                break;
            case 'o':
                if ( isdigit(argv[arg][0]) && !argv[arg][1] ) {
                    OPTIONS_values->outputColorSpace = atoi(argv[arg++]);
                }
#ifndef NO_LCMS
                else {
                    OPTIONS_values->customOutputProfileForColorSpace = argv[arg++];
                }
                break;
            case 'p':
                OPTIONS_values->cameraIccProfileFilename = argv[arg++];
#endif
                break;
            case 'P':
                OPTIONS_values->bpfile = argv[arg++];
                break;
            case 'K':
                OPTIONS_values->dark_frame = argv[arg++];
                break;
            case 'z':
                OPTIONS_values->timestamp_only = 1;
                break;
            case 'e':
                OPTIONS_values->thumbnail_only = 1;
                break;
            case 'i':
                OPTIONS_values->identify_only = 1;
                break;
            case 'c':
                OPTIONS_values->write_to_stdout = 1;
                break;
            case 'v':
                OPTIONS_values->verbose = 1;
                break;
            case 'h':
                OPTIONS_values->halfSizePreInterpolation = 1;
                break;
            case 'f':
                OPTIONS_values->fourColorRgb = 1;
                break;
            case 'A':
                for ( int c = 0; c < 4; c++ ) {
                    OPTIONS_values->greyBox[c] = atoi(argv[arg++]);
                }
            case 'a':
                OPTIONS_values->useAutoWb = 1;
                break;
            case 'w':
                OPTIONS_values->useCameraWb = 1;
                break;
            case 'M':
                OPTIONS_values->useCameraMatrix = 3 * (opm == '+');
                break;
            case 'I':
                OPTIONS_values->readFromStdin = 1;
                break;
            case 'E':
                OPTIONS_values->documentMode++;
            case 'D':
                OPTIONS_values->documentMode++;
            case 'd':
                OPTIONS_values->documentMode++;
            case 'j':
                OPTIONS_values->useFujiRotate = 0;
                break;
            case 'W':
                OPTIONS_values->noAutoBright = 1;
                break;
            case 'T':
                OPTIONS_values->outputTiff = 1;
                break;
            case '4':
                OPTIONS_values->gammaParameters[0] = OPTIONS_values->gammaParameters[1] = OPTIONS_values->noAutoBright = 1;
            case '6':
                OPTIONS_values->outputBitsPerPixel = 16;
                break;
            default:
                fprintf (stderr, "Unknown option \"-%c\".\n", opt);
                exit(1);
        }
    }

    if (arg == argc) {
        fprintf (stderr, "No files to process.\n");
        exit(1);
    }

    return arg;
}

