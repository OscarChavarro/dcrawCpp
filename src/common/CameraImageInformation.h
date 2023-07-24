#ifndef __CAMERAIMAGEINFORMATION__
#define __CAMERAIMAGEINFORMATION__

class CameraImageInformation {
  public:

    // Image
    char *inputFilename;

    CameraImageInformation();
    ~CameraImageInformation();
    void resetValues();
};

extern CameraImageInformation CAMERA_IMAGE_information;

#endif
