#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

@interface HalideView : UIView

@property CGPoint touch_position;
@property bool touch_active;
@property UILabel *statsLabel;

@property bool use_metal;
// view has a handle to the metal device when created
@property(nonatomic, readonly) id<MTLDevice> device;
// view has a handle to the metal device when created
@property(nonatomic, readonly) id<MTLCommandQueue> commandQueue;

- (void)initiateRender;

@end
