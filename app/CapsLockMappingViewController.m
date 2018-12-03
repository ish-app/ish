//
//  CapsLockMappingViewController.m
//  iSH
//
//  Created by Theodore Dubois on 12/2/18.
//

#import "CapsLockMappingViewController.h"
#import "UserPreferences.h"

@interface CapsLockMappingViewController ()

@end

@implementation CapsLockMappingViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    [UserPreferences.shared addObserver:self forKeyPath:@"capsLockMapping" options:NSKeyValueObservingOptionNew context:nil];
}

- (void)dealloc {
    [UserPreferences.shared removeObserver:self forKeyPath:@"capsLockMapping"];
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey,id> *)change context:(void *)context {
    [self.tableView reloadData];
}

- (void)tableView:(UITableView *)tableView willDisplayCell:(UITableViewCell *)cell forRowAtIndexPath:(NSIndexPath *)indexPath {
    if (cell.tag == UserPreferences.shared.capsLockMapping)
        cell.accessoryType = UITableViewCellAccessoryCheckmark;
    else
        cell.accessoryType = UITableViewCellAccessoryNone;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView cellForRowAtIndexPath:indexPath];
    UserPreferences.shared.capsLockMapping = cell.tag;
}

@end
