#ifndef LOAD_SAVE_PNG_HPP
#define LOAD_SAVE_PNG_HPP

#include <string>
#include <vector>
#include <stdint.h>

/*
 * Load and save PNG files.
 * NOTE: these functions perform flipping to place keep the
 *       pixel origin in the LOWER LEFT!
 */

bool load_png(std::string filename, unsigned int &width, unsigned int &height, std::vector< uint32_t > &data);
void save_png(std::string filename, unsigned int width, unsigned int height, uint32_t const *data);

#endif
