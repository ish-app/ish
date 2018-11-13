//
//  ThemeViewController.m
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import "ThemeViewController.h"
#import "UserPreferences.h"

@interface ThemeViewController ()

@end

@implementation ThemeViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    [self.tableView registerClass:[UITableViewCell class] forCellReuseIdentifier:@"ExampleCell"];
    [self.tableView registerClass:[UITableViewCell class] forCellReuseIdentifier:@"ThemeNameCell"];
    
    [[UserPreferences shared] addObserver:self forKeyPath:@"theme" options:NSKeyValueObservingOptionNew context:nil];
}

- (void)dealloc
{
    @try {
        [[UserPreferences shared] removeObserver:self forKeyPath:@"theme"];
    } @catch (NSException * __unused exception) {}
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context
{
    [self.tableView reloadData];
}

#pragma mark - Table view data source

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return 2;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    if (section == 0) {
        return UserPreferenceThemeCount;
    } else if (section == 1) {
        // Example row to show selected theme
        return 1;
    }
    
    assert("unhandled section");
    return 0;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    
    // reuse example cell separately to avoid reuse issues
    NSString *reuseIdentifier = indexPath.section == 1 ? @"ExampleCell" : @"ThemeNameCell";
    UserPreferences *prefs = [UserPreferences shared];
    
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:reuseIdentifier forIndexPath:indexPath];
    
    if (indexPath.section == 0) {
        cell.textLabel.text = ThemeName(indexPath.row);
        cell.accessoryType = indexPath.row == prefs.theme ? UITableViewCellAccessoryCheckmark : UITableViewCellAccessoryNone;
    } else if (indexPath.section == 1) {
        cell.backgroundColor = ThemeBackgroundColor(prefs.theme);
        cell.textLabel.textColor = ThemeForegroundColor(prefs.theme);
        cell.textLabel.font = [UIFont fontWithName:@"Menlo-Regular" size:17.0];
        cell.textLabel.text = [NSString stringWithFormat:@"%@:~# ps aux", [UIDevice currentDevice].name];
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
    }
    
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    
    // selection only necessary for theme name list
    if (indexPath.section != 0) {
        return;
    }
    
    [[UserPreferences shared] setTheme:indexPath.row];
}

@end
