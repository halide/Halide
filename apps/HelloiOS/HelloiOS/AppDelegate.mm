#import "AppDelegate.h"
#import "HalideView.h"
#import "HalideViewController.h"

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];

    HalideViewController *halide_view_controller = [[HalideViewController alloc] init];

    self.window.rootViewController = halide_view_controller;
        
    const int kBorderX = 10;
    const int kBorderY = 40;

    // Add a view for image output
    int image_width, image_height;
    HalideView *output_image = [HalideView alloc];
    {
        CGRect box = self.window.frame;
        box.origin.x += kBorderX;
        box.origin.y += kBorderY;
        box.size.width -= kBorderX*2;
        box.size.height -= kBorderY*2;
        image_width = box.size.width;
        image_height = box.size.height;
        output_image = [output_image initWithFrame:box];
        output_image.backgroundColor = [UIColor blackColor];
        [self.window addSubview: output_image];
        [output_image setUserInteractionEnabled:true];
#if HAS_METAL_SDK
        output_image.use_metal = true;
#endif
    }
    halide_view_controller.halide_view = output_image;
    

    // Add a view for text output
    UITextView *output_log = [UITextView alloc];
    {
        CGRect box = self.window.frame;
        box.origin.x += kBorderX;
        box.origin.y += kBorderY + image_height;
        box.size.width -= box.origin.x + kBorderX;
        box.size.height -= image_height + kBorderY;
        output_log = [output_log initWithFrame: box];
        UIFont *font = [UIFont systemFontOfSize:20];
        [output_log setFont:font];
        [self.window addSubview: output_log];
    }
    output_image.outputLog = output_log;
    
    // Override point for customization after application launch.
    self.window.backgroundColor = [UIColor whiteColor];
    [self.window makeKeyAndVisible];
    
    return YES;
}

- (void)applicationWillResignActive:(UIApplication *)application
{
    // Sent when the application is about to move from active to inactive state. This can occur for certain types of temporary interruptions (such as an incoming phone call or SMS message) or when the user quits the application and it begins the transition to the background state.
    // Use this method to pause ongoing tasks, disable timers, and throttle down OpenGL ES frame rates. Games should use this method to pause the game.
}

- (void)applicationDidEnterBackground:(UIApplication *)application
{
    // Use this method to release shared resources, save user data, invalidate timers, and store enough application state information to restore your application to its current state in case it is terminated later. 
    // If your application supports background execution, this method is called instead of applicationWillTerminate: when the user quits.
}

- (void)applicationWillEnterForeground:(UIApplication *)application
{
    // Called as part of the transition from the background to the inactive state; here you can undo many of the changes made on entering the background.
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
    // Restart any tasks that were paused (or not yet started) while the application was inactive. If the application was previously in the background, optionally refresh the user interface.
    
}

- (void)applicationWillTerminate:(UIApplication *)application
{
    // Called when the application is about to terminate. Save data if appropriate. See also applicationDidEnterBackground:.
}


@end
