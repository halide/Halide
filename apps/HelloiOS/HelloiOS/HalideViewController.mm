#import "HalideViewController.h"
#import "HalideView.h"

@implementation HalideViewController {
    HalideView *_halideView;
    UILabel *_statsLabel;
}

- (void)loadView {
    // Create the HalideView as the main view
    _halideView = [[HalideView alloc] initWithFrame:CGRectZero];
    _halideView.backgroundColor = [UIColor blackColor];

    _halideView.use_metal = YES;

    self.view = _halideView;

    // Add stats label at the bottom
    _statsLabel = [[UILabel alloc] init];
    _statsLabel.textColor = [UIColor whiteColor];
    _statsLabel.font = [UIFont systemFontOfSize:14];
    _statsLabel.numberOfLines = 0;
    _statsLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_statsLabel];

    // Pin label to bottom with safe area
    [NSLayoutConstraint activateConstraints:@[
        [_statsLabel.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:10],
        [_statsLabel.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-10],
        [_statsLabel.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor constant:-10]
    ]];

    // Give HalideView a reference to the label for updates
    _halideView.statsLabel = _statsLabel;
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [_halideView initiateRender];
}

@end
