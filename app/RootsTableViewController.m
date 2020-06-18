//
//  RootsTableViewController.m
//  iSH
//
//  Created by Theodore Dubois on 6/7/20.
//

#import "Roots.h"
#import "RootsTableViewController.h"
#import "UIApplication+OpenURL.h"
#import "UIViewController+Extras.h"

@interface RootsTableViewController ()
@end

@interface RootDetailViewController : UITableViewController

@property (nonatomic) NSString *rootName;
@property (weak, nonatomic) IBOutlet UILabel *nameLabel;
@property (weak, nonatomic) IBOutlet UILabel *deleteLabel;
@property (weak, nonatomic) IBOutlet UITableViewCell *deleteCell;

@end

@implementation RootsTableViewController

- (void)awakeFromNib {
    [super awakeFromNib];
    [Roots.instance addObserver:self forKeyPath:@"roots" options:0 context:nil];
    [Roots.instance addObserver:self forKeyPath:@"defaultRoot" options:0 context:nil];
    NSLog(@"%@ hi", self);
}
- (void)dealloc {
    NSLog(@"%@ bye", self);
    [Roots.instance removeObserver:self forKeyPath:@"roots"];
    [Roots.instance removeObserver:self forKeyPath:@"defaultRoot"];
}
- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey,id> *)change context:(void *)context {
    [self.tableView reloadData];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return 1;
}
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return Roots.instance.roots.count;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    NSString *ident = @"Root";
    if ([Roots.instance.roots[indexPath.row] isEqual:Roots.instance.defaultRoot])
        ident = @"Default Root";
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:ident forIndexPath:indexPath];
    cell.textLabel.text = Roots.instance.roots[indexPath.row];
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
}

- (void)prepareForSegue:(UIStoryboardSegue *)segue sender:(id)sender {
    RootDetailViewController *vc = segue.destinationViewController;
    vc.rootName = Roots.instance.roots[self.tableView.indexPathForSelectedRow.row];
}

- (IBAction)importFilesystem:(id)sender {
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
                                              initWithDocumentTypes:@[@"public.tar-archive", @"org.gnu.gnu-zip-archive"]
                                              inMode:UIDocumentPickerModeImport];
    picker.navigationItem.prompt = @"Select a tarball to import";
    [self presentViewController:picker animated:YES completion:nil];
    if (@available(iOS 13, *)) {
        picker.shouldShowFileExtensions = YES;
    }
    picker.delegate = self;
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSAssert(urls.count == 1, @"somehow picked multiple documents");
    NSURL *url = urls[0];

    NSString *fileName = url.lastPathComponent.stringByDeletingPathExtension;
    if ([fileName hasSuffix:@".tar"])
        fileName = fileName.stringByDeletingPathExtension;
    unsigned i = 2;
    NSString *name = fileName;
    while ([Roots.instance.roots containsObject:name]) {
        name = [NSString stringWithFormat:@"%@ %u", fileName, i++];
    }
    NSError *error;
    if (![Roots.instance importRootFromArchive:urls[0] name:name error:&error]) {
        [self presentError:error title:@"Import failed"];
    }
}

@end

@implementation RootDetailViewController

- (void)viewWillAppear:(BOOL)animated {
    self.navigationItem.title = self.rootName;
    self.nameLabel.text = self.rootName;
    [self update];
}

- (void)update {
    self.deleteLabel.enabled = !self.isDefaultRoot;
    self.deleteCell.selectionStyle = !self.isDefaultRoot ? UITableViewCellSelectionStyleDefault : UITableViewCellSelectionStyleNone;
    [self.tableView reloadData];
}

- (BOOL)isDefaultRoot {
    return [self.rootName isEqualToString:Roots.instance.defaultRoot];
}

- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    if (section == 2) { // delete
        if (self.isDefaultRoot)
            return @"This filesystem can't be deleted because it's currently mounted as the root.";
    }
    return [super tableView:tableView titleForFooterInSection:section];
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    if (indexPath.section == 0 && indexPath.row == 1) {
        // browse files
        NSURL *url = [NSFileProviderManager.defaultManager.documentStorageURL URLByAppendingPathComponent:self.rootName];
        NSURLComponents *components = [NSURLComponents componentsWithURL:url resolvingAgainstBaseURL:NO];
        components.scheme = @"shareddocuments";
        [UIApplication openURL:components.string];
    }
    if (indexPath.section == 1 && indexPath.row == 0) {
        // boot this
        Roots.instance.defaultRoot = self.rootName;
        exit(0);
    }
    if (indexPath.section == 2 && indexPath.row == 0) {
        // delete
        if (!self.isDefaultRoot)
            [self deleteFilesystem:nil];
    }
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
}

- (void)deleteFilesystem:(id)sender {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Really delete?"
                                                                   message:@"I can't be bothered to implement any undo or regret UI so this is irreversable."
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Delete" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *action) {
        NSError *error;
        if (![Roots.instance destroyRootNamed:self.rootName error:&error]) {
            [self presentError:error title:@"Delete failed"];
        } else {
            [self.navigationController popViewControllerAnimated:YES];
        }
    }]];
    [self presentViewController:alert animated:YES completion:nil];
}


@end
