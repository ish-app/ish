//
//  FontPickerViewController.m
//  iSH
//
//  Created by Theodore Dubois on 10/26/19.
//

#import "FontPickerViewController.h"
#import "UserPreferences.h"

@interface FontPickerViewController ()

@property NSArray<NSString *> *fontFamilies;

@end

@implementation FontPickerViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    NSMutableArray *families = [NSMutableArray new];
    for (NSString *family in UIFont.familyNames) {
        UIFont *font = [UIFont fontWithName:family size:1];
        if (font.fontDescriptor.symbolicTraits & UIFontDescriptorTraitMonoSpace) {
            [families addObject:family];
        }
    }
    self.fontFamilies = families;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return self.fontFamilies.count;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:@"Font"];
    NSString *family = self.fontFamilies[indexPath.row];
    UIFont *font = [UIFont fontWithName:family size:18];
    cell.textLabel.font = [[UIFontMetrics metricsForTextStyle:UIFontTextStyleBody] scaledFontForFont:font];
    cell.textLabel.adjustsFontForContentSizeCategory = YES;
    cell.textLabel.text = family;
    if ([family isEqualToString:UserPreferences.shared.fontFamily])
        cell.accessoryType = UITableViewCellAccessoryCheckmark;
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    UserPreferences.shared.fontFamily = self.fontFamilies[indexPath.row];
    [self.navigationController popViewControllerAnimated:YES];
}

@end
