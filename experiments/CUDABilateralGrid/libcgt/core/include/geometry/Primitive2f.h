#ifndef PRIMITIVE_2F_H
#define PRIMITIVE_2F_H

#include "vecmath/Rect2f.h"

// interface
class Primitive2f
{
public:

	virtual Rect2f boundingBox() = 0;

};

#endif // PRIMITIVE_2F_H
