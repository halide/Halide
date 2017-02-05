#import "HalideViewController.h"
#import "HalideView.h"
#import <UIKit/UIKit.h>

@implementation HalideViewController
{
    HalideView *_halide_view;
}

- (void)viewWillAppear:(BOOL)animated
{
    [super viewWillAppear:animated];
    [_halide_view initiateRender];
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event {
    [_halide_view touchesBegan:touches withEvent:event];
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event {
    [_halide_view touchesMoved:touches withEvent:event];
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event {
    [_halide_view touchesEnded:touches withEvent:event];
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event {
    [_halide_view touchesCancelled:touches withEvent:event];
}


@end
