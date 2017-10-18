//
//  ViewController.m
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import "TerminalViewController.h"
#include "fs/tty.h"

@interface TerminalViewController ()

@end

static TerminalViewController *tvc;

@implementation TerminalViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    // Do any additional setup after loading the view, typically from a nib.
    tvc = self;
}


- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

- (int)write:(const void *)buf length:(size_t)len {
    NSString *string = [[NSString alloc] initWithBytes:buf length:len encoding:NSUTF8StringEncoding];
    NSLog(@"%@", string);
    return 0;
}

@end

static size_t ios_tty_write(struct tty *tty, const void *buf, size_t len) {
    return [tvc write:buf length: len];
}

struct tty_driver ios_tty_driver = {
    .write = ios_tty_write,
};
