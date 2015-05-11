//
//  ViewController.h
//  test_ios
//
//  Created by abstephens on 1/20/15.
//
//

#import <UIKit/UIKit.h>

@interface ViewController : UIViewController <UIWebViewDelegate>

@property (retain) UIWebView *outputView;
@property (retain) NSMutableDictionary* database;

@end
