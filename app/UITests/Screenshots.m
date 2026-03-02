//
//  Screenshots.m
//  iSHUITests
//
//  Created by Theodore Dubois on 12/18/20.
//

#import <UIKit/UIKit.h>
#import <XCTest/XCTest.h>
#import "iSHUITests-Swift.h"

@interface Screenshots : XCTestCase

@property XCUIApplication *app;

@end

@implementation Screenshots

- (void)setUp {
    self.continueAfterFailure = NO;
    XCUIApplication *app = self.app = [XCUIApplication new];
    [Snapshot setupSnapshot:app waitForAnimations:NO];
    NSString *hostnameOverride = nil;
    switch (UIDevice.currentDevice.userInterfaceIdiom) {
        case UIUserInterfaceIdiomPad: hostnameOverride = @"iPad"; break;
        case UIUserInterfaceIdiomPhone: hostnameOverride = @"iPhone"; break;
        default: XCTFail(@"unknown UI idiom");
    }
    app.launchArguments = [app.launchArguments arrayByAddingObjectsFromArray:@[@"-hostnameOverride", hostnameOverride]];
    [app launch];
    XCTAssert([app.webViews.staticTexts.firstMatch waitForExistenceWithTimeout:10]);
    [self chooseTheme:@"Solarized"];
}

- (XCUIElementQuery *)terminalLines {
    return self.app.webViews.staticTexts;
}

- (XCUIElementQuery *)terminalLinesContaining:(NSString *)text {
    return [self.terminalLines matchingPredicate:[NSPredicate predicateWithFormat:@"label CONTAINS %@", text]];
}

- (void)waitForTerminalText:(NSString *)text timeout:(NSUInteger)timeout {
    XCTAssert([[self terminalLinesContaining:text].firstMatch waitForExistenceWithTimeout:timeout]);
}

- (void)waitForPromptWithTimeout:(NSUInteger)timeout {
    // Waits for the last text thing to be a prompt
    XCUIElementQuery *terminalLines = self.terminalLines;
    NSPredicate *pred = [NSPredicate predicateWithBlock:^BOOL(id _, NSDictionary *__) {
        XCUIElement *lastLine = nil;
        // Loop this because -count and -elementBoundByIndex will take two separate UI snapshots, so the index could become invalid.
        while (!lastLine.exists)
            lastLine = terminalLines.allElementsBoundByIndex.lastObject;
        NSString *lastLineText = lastLine.label;
        XCTAssertNotNil(lastLineText);
        return [lastLineText hasSuffix:@":~#"];
    }];
    [self waitForExpectations:@[[self expectationForPredicate:pred evaluatedWithObject:nil handler:nil]] timeout:timeout];
}

- (void)runCommand:(NSString *)command timeout:(NSUInteger)timeout {
    [self.app typeText:[NSString stringWithFormat:@"%@\n", command]];
    [self waitForPromptWithTimeout:timeout];
}

- (void)chooseTheme:(NSString *)name {
    [self.app.buttons[@"Settings"] tap];
    [self.app.tables.staticTexts[@"Appearance"] tap];
    [self.app.tables.staticTexts[@"Theme"] tap];
    [self.app.tables.staticTexts[name] tap];
    [self.app.navigationBars[@"Themes"].buttons[@"Appearance"] tap];
    [self.app.navigationBars[@"Appearance"].buttons[@"Settings"] tap];
    [self.app.navigationBars[@"Settings"].buttons[@"Done"] tap];
}

- (void)snapshot:(NSString *)name order:(NSUInteger)order {
    name = [NSString stringWithFormat:@"%02u%@", (unsigned) order, name];
    [Snapshot snapshot:name timeWaitingForIdle:10];
}

- (void)testSystemInfo {
    [self runCommand:@"uname -a" timeout:5];
    [self snapshot:@"systeminfo" order:1];
}

- (void)testLanguages {
    [self runCommand:@"apk add build-base python3" timeout:120];
    [self runCommand:@"printf '#include <stdio.h>\\nint main() { printf(\"Hello, iSH!\\\\n\"); }' > hello.c" timeout:5];
    [self runCommand:@"gcc hello.c && ./a.out" timeout:5];
    [self runCommand:@"python3 -c 'print(\"Hello, iSH!\")'" timeout:5];
    [self snapshot:@"languages" order:2];
}

- (void)testEditorsInTmux {
    [self runCommand:@"apk add vim nano tmux" timeout:120];
    [self runCommand:@"tmux new-session -d -s foo nano" timeout:5];
    [self runCommand:@"tmux split-window -v vim" timeout:5];
    [self runCommand:@"tmux select-layout even-vertical" timeout:5];
    [self.app typeText:@"tmux attach -t foo\n"];
    [self waitForTerminalText:@"GNU nano" timeout:30];
    [self snapshot:@"editorsintmux" order:3];
}

- (void)testEmacs {
    [self runCommand:@"apk add emacs" timeout:120];
    [self.app typeText:@"emacs\n"];
    [self waitForTerminalText:@"Welcome to GNU Emacs" timeout:30];
    [self snapshot:@"emacs" order:4];
}

@end
