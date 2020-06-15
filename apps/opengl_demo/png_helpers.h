#ifndef _PNG_HELPERS_
#define _PNG_HELPERS_

namespace PNGHelpers {

struct image_info {
    unsigned int width;
    unsigned int height;
    const uint8_t *data;
};

struct image_info load(const std::string &filepath);
}  // namespace PNGHelpers

#endif
