#ifndef HelloiOS_HalideViewController_h
#define HelloiOS_HalideViewController_h

#import "HalideView.h"
#import <UIKit/UIKit.h>


@interface HalideViewController : UIViewController

@property HalideView *halide_view;

- (void)viewWillAppear:(BOOL)animated;

@end

#endif