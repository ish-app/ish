//
//  IconViewController.m
//  iSH
//
//  Created by Theodore Dubois on 12/13/19.
//

#import "AltIconViewController.h"
#import "UIApplication+OpenURL.h"

@interface AltIconViewController ()

@property (weak) IBOutlet UICollectionView *collectionView;

@property NSDictionary<NSString *, NSDictionary *> *altIcons;
@property NSArray<NSString *> *altIconNames;

@end

@interface AltIconCell : UICollectionViewCell

@property (weak, nonatomic) IBOutlet UIImageView *imageView;
@property (weak, nonatomic) IBOutlet UIImageView *checkboxImageView;
@property (weak, nonatomic) IBOutlet UIButton *authorButton;

@property (nonatomic) NSString *link;

- (void)updateImage:(UIImage *)image description:(NSString *)description author:(NSString *)author link:(NSURL *)link;

@end

@implementation AltIconViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.altIcons = [NSDictionary dictionaryWithContentsOfURL:
                     [NSBundle.mainBundle URLForResource:@"Icons"
                                           withExtension:@"plist"]];
    self.altIconNames = [self.altIcons.allKeys sortedArrayUsingSelector:@selector(compare:)];
    
    NSString *iconName = UIApplication.sharedApplication.alternateIconName;
    if (iconName == nil)
        iconName = @"";
    NSIndexPath *indexPath = [NSIndexPath indexPathForItem:[self.altIconNames indexOfObject:iconName]
                                                inSection:0];
    [self.collectionView selectItemAtIndexPath:indexPath
                                      animated:NO
                                scrollPosition:UICollectionViewScrollPositionTop];
//    UICollectionViewFlowLayout *layout = self.collectionView.collectionViewLayout;
//    layout.sectionFootersPinToVisibleBounds = YES;
}

- (NSInteger)collectionView:(UICollectionView *)collectionView numberOfItemsInSection:(NSInteger)section {
    return self.altIconNames.count;
}

- (UICollectionReusableView *)collectionView:(UICollectionView *)collectionView viewForSupplementaryElementOfKind:(NSString *)kind atIndexPath:(NSIndexPath *)indexPath {
    return [collectionView dequeueReusableSupplementaryViewOfKind:kind withReuseIdentifier:@"footer" forIndexPath:indexPath];
}

- (UICollectionViewCell *)collectionView:(UICollectionView *)collectionView cellForItemAtIndexPath:(NSIndexPath *)indexPath {
    AltIconCell *cell = [collectionView dequeueReusableCellWithReuseIdentifier:@"icon" forIndexPath:indexPath];
    NSString *iconName = self.altIconNames[indexPath.item];
    [cell updateImage:[UIImage imageNamed:iconName.length == 0 ? @"icon" : iconName]
          description:self.altIcons[iconName][@"description"]
               author:self.altIcons[iconName][@"author"]
                 link:self.altIcons[iconName][@"link"]];
    return cell;
}

- (void)collectionView:(UICollectionView *)collectionView didSelectItemAtIndexPath:(NSIndexPath *)indexPath {
    NSString *iconName = self.altIconNames[indexPath.item];
    if (iconName.length == 0)
        iconName = nil;
    [UIApplication.sharedApplication setAlternateIconName:iconName completionHandler:^(NSError *err) {
        if (err != nil)
            NSLog(@"%@", err);
    }];
}

- (IBAction)openSubmissions:(id)sender {
    [UIApplication openURL:@"https://github.com/tbodt/ish/issues/578"];
}

- (CGFloat)sideInset:(UICollectionViewFlowLayout *)layout {
    // For maximum aesthetics, there should be a decent amount of spacing between cells
    static const CGFloat kMinSpacer = 20;
    // The insets should be somewhat smaller than the spacer
    static const CGFloat kInsetToSpacerRatio = 0.75;
    
    CGFloat total = layout.collectionView.frame.size.width;
    CGFloat item = layout.itemSize.width;
    NSUInteger count = (int) (total / item);
    CGFloat spacer;
    CGFloat inset;
    do {
        CGFloat slack = total - (item * count);
        spacer = slack / (2 * kInsetToSpacerRatio + count - 1);
        inset = spacer * kInsetToSpacerRatio;
        count--;
    } while (spacer < kMinSpacer);
    return inset;
}
- (UIEdgeInsets)collectionView:(UICollectionView *)collectionView layout:(UICollectionViewFlowLayout *)layout insetForSectionAtIndex:(NSInteger)section {
    CGFloat sideInset = [self sideInset:layout];
    return UIEdgeInsetsMake(sideInset, sideInset, 20, sideInset);
}

@end

@implementation AltIconCell

- (void)awakeFromNib {
    [super awakeFromNib];
    
    CAShapeLayer *iconMask = [CAShapeLayer new];
    iconMask.frame = self.imageView.bounds;
    iconMask.path = [UIBezierPath bezierPathWithRoundedRect:self.imageView.bounds
                                               cornerRadius:self.imageView.bounds.size.width * 0.225].CGPath;
    self.imageView.layer.mask = iconMask;
    self.imageView.layer.minificationFilter = kCAFilterTrilinear;
    
    if (@available(iOS 13, *)) {
        self.checkboxImageView.image = UIImage.checkmarkImage;
    } else {
//        self.checkboxImageView.backgroundColor = UIColor.whiteColor;
//        self.checkboxImageView.layer.cornerRadius = self.checkboxImageView.bounds.size.width / 2;
    }

    self.authorButton.titleLabel.adjustsFontForContentSizeCategory = YES;

    self.isAccessibilityElement = YES;
    self.accessibilityCustomActions = @[[[UIAccessibilityCustomAction alloc] initWithName:@"Open link" target:self selector:@selector(openSource:)]];
}

- (void)updateImage:(UIImage *)image description:(NSString *)description author:(NSString *)author link:(NSString *)url {
    self.imageView.image = image;
    [self.authorButton setTitle:[NSString stringWithFormat:@"by %@", author] forState:UIControlStateNormal];
    self.link = url;
    self.accessibilityLabel = [NSString stringWithFormat:@"%@ by %@", description, author];
}

- (IBAction)openSource:(id)sender {
    [UIApplication openURL:self.link];
}

- (void)setSelected:(BOOL)selected {
    [super setSelected:selected];
    self.checkboxImageView.hidden = !selected;
    self.accessibilityTraits = selected ? UIAccessibilityTraitSelected : 0;
}

@end
