//
//  HalideView.m
//  Halide test
//
//  Created by Andrew Adams on 7/23/14.
//  Copyright (c) 2014 Andrew Adams. All rights reserved.
//

#import "HalideView.h"

@implementation HalideView

- (id)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    return self;
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event {
    self.touch_position = [touches.anyObject locationInView:self];
    self.touch_active = true;
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event {
    self.touch_position = [touches.anyObject locationInView:self];
    self.touch_active = true;
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event {
    self.touch_active = false;
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event {
    self.touch_active = false;
}

@end
