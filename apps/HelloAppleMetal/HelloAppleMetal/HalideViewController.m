//
//  HalideViewController.m
//  HelloAppleMetal
//
//  Created by Z Stern on 8/10/15.
//  Copyright (c) 2015 Andrew Adams. All rights reserved.
//

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

@end
