#ifndef __OPTIONS__
#define __OPTIONS__

#define DCRAW_VERSION "9.28cpp"

class Options {
  private:
    static void printHelp(const char **argv);

  public:
    int user_flip;
    int timestamp_only;
    int thumbnail_only;
    int identify_only;
    int useFujiRotate;
    int readFromStdin;
    const char *bpfile;
    const char *dark_frame;
    int user_qual;
    int user_black;
    int user_sat;
#ifndef NO_LCMS
    const char *cameraIccProfileFilename;
    const char *customOutputProfileForColorSpace;
#endif
    int write_to_stdout;

    unsigned shotSelect;
    unsigned multiOut;
    double chromaticAberrationCorrection[4];
    double gammaParameters[6];
    float brightness;
    float threshold;
    int halfSizePreInterpolation;
    int fourColorRgb;
    int documentMode;
    int highlight;
    int verbose;
    int useAutoWb;
    int useCameraWb;
    int useCameraMatrix;
    int outputColorSpace;
    int outputBitsPerPixel;
    int outputTiff;
    int med_passes;
    int noAutoBright;
    unsigned greyBox[4];
    float userMul[4];

    Options();
    ~Options();
    int setArguments(int argc, const char **argv);
};

extern Options *OPTIONS_values; // Configurations from program arguments

#endif
