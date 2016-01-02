//
//  HalideViewController.h
//  HelloAppleMetal
//
//  Created by Z Stern on 8/10/15.
//  Copyright (c) 2015 Andrew Adams. All rights reserved.
//

#ifndef HelloAppleMetal_HalideViewController_h
#define HelloAppleMetal_HalideViewController_h

#import "HalideView.h"
#import <UIKit/UIKit.h>

@interface HalideViewController : UIViewController

@property HalideView *halide_view;

- (void)viewWillAppear:(BOOL)animated;

@end

#endif
