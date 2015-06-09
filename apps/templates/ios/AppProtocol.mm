//
//  AppProtocol.m
//  test_osx
//
//  Created by abstephens on 1/22/15.
//
//
#import "AppProtocol.h"
#import "AppDelegate.h"
#import "ViewController.h"
#import "HalideRuntime.h"
#import <dlfcn.h>

NSString* kAppProtocolURLScheme = @"app";

// This protocol returns images to the app's WebView in response to app://
// requests. It provides the same functionality on OS X, and on iOS where
// interaction between the app and the WebView javascript context is very
// limited. On other platforms like pnacl, images are returned directly to the
// javascript context in the Chrome WebView and a protocol like this is not
// necessary.
@implementation AppProtocol

+ (BOOL)canInitWithRequest:(NSURLRequest *)request {

  // Check if the request matches the app custom URL scheme
  return [request.URL.scheme isEqualToString:kAppProtocolURLScheme];
}

+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request {

  // Caching is not supported by this protocol so canonicalization of the
  // request is not necessary.
  return request;
}

+ (BOOL)requestIsCacheEquivalent:(NSURLRequest *)a toRequest:(NSURLRequest *)b {

  // Caching is not supported by this protocol, each request results in a
  // the test app or halide runtime performing some action which may have side
  // effects
  return NO;
}

- (void)startLoading {

  // Look up the URL in the app database
  ViewController* ctrl = (ViewController*)[UIApplication sharedApplication].delegate.window.rootViewController;

  NSString* key = self.request.URL.absoluteString;

  // Lookup the key in the image database
  NSData* responseData = ctrl.database[key];

  if (!responseData) {
    // Otherwise, return a placeholder image
    responseData = UIImagePNGRepresentation([UIImage imageNamed:@"none"]);
  }

  NSString* responseMimeType = @"image/png";

  // Create a response to send back to the webview
  NSURLResponse *response = [[NSURLResponse alloc] initWithURL:self.request.URL
                                                      MIMEType:responseMimeType
                                         expectedContentLength:responseData.length
                                              textEncodingName:nil];

  // Send the initial response
  [self.client URLProtocol:self
        didReceiveResponse:response
        cacheStoragePolicy:NSURLCacheStorageNotAllowed];

  // Send the data
  [self.client URLProtocol:self didLoadData:responseData];

  // Finish
  [self.client URLProtocolDidFinishLoading:self];
}

- (void)stopLoading {

  // Operations in this protocol should be nearly instantaneous, so this
  // method is empty.
}

@end
