//
//  ExportViewController.m
//  iSH
//
//  Created by Hugo Gonzalez on 11/12/18.
//

#import "ExportViewController.h"

@interface ExportViewController ()
@end

@implementation ExportViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.allowsMultipleSelection = true;
    self.delegate = self;
}

- (IBAction)cancelTapped:(UIBarButtonItem *)sender {
    [self dismissViewControllerAnimated:true completion:nil];
}

#pragma mark - UIDocumentPickeDelegate
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
}

@end
