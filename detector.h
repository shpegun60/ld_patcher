#ifndef DETECTOR_H
#define DETECTOR_H

#include "catalogtypes.h"

class SourcePackage;

class Detector
{
public:
    static ProfileMatchResult matchBestProfile(const SourcePackage &source,
                                               const CatalogData &catalog,
                                               QString *errorMessage);
};

#endif // DETECTOR_H
