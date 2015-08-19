//
//  HalideView.h
//  Halide test
//
//  Created by Andrew Adams on 7/23/14.
//  Copyright (c) 2014 Andrew Adams. All rights reserved.
//

#import <QuartzCore/QuartzCore.h>
#import <Metal/Metal.h>
#import <UIKit/UIKit.h>

@interface HalideView : UIView

@property CGPoint touch_position;
@property bool touch_active;

@property UITextView *outputLog;

// view has a handle to the metal device when created
@property (nonatomic, readonly) id <MTLDevice> device;

// view has a handle to the metal device when created
@property (nonatomic, readonly) id <MTLCommandQueue> commandQueue;

// view controller will be call off the main thread
//- (void)displayLayer:(CALayer *)layer;

- (void)initiateRender;

- (void)displayRender:(id <MTLDrawable>)drawable;

@end
