//
//  CustomThemeViewController.m
//  iSH
//
//  Created by Corban Amouzou on 2019-10-19.
//

#import "CustomThemeViewController.h"
#import "UserPreferences.h"
static NSString *const ForegroundCellIdentifier = @"Foreground Color";
static NSString *const BackgroundCellIdentifier = @"Background Color";
static NSString *const PreiviewCellIdentifier = @"Preview";

@interface CustomThemeViewController ()

@end

@implementation CustomThemeViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [[UserPreferences shared] addObserver:self forKeyPath:@"Foreground Color" options:NSKeyValueObservingOptionNew context:nil];
    [[UserPreferences shared] addObserver:self forKeyPath:@"Background Color" options:NSKeyValueObservingOptionNew context:nil];
    [[UserPreferences shared] addObserver:self forKeyPath:@"Preview" options:NSKeyValueObservingOptionNew context:nil];
}

- (void)dealloc {
    @try {
        [[UserPreferences shared] removeObserver:self forKeyPath:@"Foreground Color"];
        [[UserPreferences shared] removeObserver:self forKeyPath:@"Background Color"];
        [[UserPreferences shared] removeObserver:self forKeyPath:@"Preview"];
    } @catch (NSException * __unused exception) {}
    
}
- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context {
[self.tableView reloadData];
[self setNeedsStatusBarAppearanceUpdate];
}
#pragma mark - The same stuff as AboutApearanceViewController.m
/* Hey Dont Judge lol, only thirteen! yeash */
enum {
    CustomForegroundSection,
    CustomBackgroundSection,
    PreviewSection,
    SectionCount,
};

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return SectionCount;
}

-(NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    switch (section) {
        case CustomForegroundSection: return 1;
        case CustomBackgroundSection: return 1;
        case PreviewSection: return 1;
         default: NSAssert(NO, @"unhandled section"); return 0;

    }
}
- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
switch (section) {
    case CustomBackgroundSection : return @"Background Colors";
    case CustomForegroundSection : return @"Foreground Colors";
    case PreviewSection : return @"Preview";
    default: return nil;
    }
}
- (NSString *)reuseIdentifierForSection:(NSInteger)section {
switch (section) {
    case CustomBackgroundSection : return @"Background Color";
    case CustomForegroundSection : return @"Foreground Color";
    case PreviewSection : return @"Preview";
    default: return nil;
    }
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
UserPreferences *prefs = [UserPreferences shared];
UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:[self reuseIdentifierForSection:indexPath.section] forIndexPath:indexPath];
     switch (indexPath.section) {
         case PreviewSection:
               cell.backgroundColor = prefs.theme.backgroundColor;
               cell.textLabel.textColor = prefs.theme.foregroundColor;
               cell.textLabel.font = [UIFont fontWithName:@"Menlo-Regular" size:prefs.fontSize.doubleValue];
               cell.textLabel.text = [NSString stringWithFormat:@"%@:~# Command Line Preview", [UIDevice currentDevice].name];
               cell.selectionStyle = UITableViewCellSelectionStyleNone;
               break;
             
             
             
        }
    return cell;
    
}

@end
