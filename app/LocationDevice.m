//
//  LocationDevice.m
//  iSH
//
//  Created by Theodore Dubois on 10/20/19.
//

#import <Foundation/Foundation.h>
#import <CoreLocation/CoreLocation.h>
#include "kernel/fs.h"
#include "fs/dev.h"
#include "util/sync.h"

@interface LocationTracker : NSObject <CLLocationManagerDelegate>

+ (LocationTracker *)instance;

@property CLLocationManager *locationManager;
@property (nonatomic) CLLocation *latest;
@property lock_t lock;
@property cond_t updateCond;

- (int)waitForUpdate;

@end

BOOL CLIsAuthorized(CLAuthorizationStatus status) {
    return status == kCLAuthorizationStatusAuthorizedWhenInUse || status == kCLAuthorizationStatusAuthorizedAlways;
}

@implementation LocationTracker

+ (LocationTracker *)instance {
    static __weak LocationTracker *tracker;
    if (tracker == nil) {
        __block LocationTracker *newTracker;
        dispatch_sync(dispatch_get_main_queue(), ^{
            if (tracker == nil) {
                newTracker = [LocationTracker new];
                tracker = newTracker;
            }
        });
        return newTracker;
    }
    return tracker;
}

- (instancetype)init {
    if (self = [super init]) {
        self.locationManager = [CLLocationManager new];
        self.locationManager.delegate = self;
        self.locationManager.allowsBackgroundLocationUpdates = YES;
        if (CLIsAuthorized([CLLocationManager authorizationStatus])) {
            [self.locationManager startUpdatingLocation];
            [self.locationManager requestLocation];
        } else {
            [self.locationManager requestAlwaysAuthorization];
        }
        
        lock_init(&_lock);
        cond_init(&_updateCond);
    }
    return self;
}

- (void)locationManager:(CLLocationManager *)manager didUpdateLocations:(NSArray<CLLocation *> *)locations {
    lock(&_lock);
    self.latest = locations.lastObject;
    notify(&_updateCond);
    unlock(&_lock);
}

- (void)locationManager:(CLLocationManager *)manager didFailWithError:(NSError *)error {
    NSLog(@"location failed %@", error);
}

- (void)dealloc {
    [self.locationManager stopUpdatingLocation];
    cond_destroy(&_updateCond);
}

- (int)waitForUpdate {
    lock(&_lock);
    CLLocation *oldLatest = self.latest;
    int err = 0;
    while (self.latest == oldLatest) {
        err = wait_for(&_updateCond, &_lock, NULL);
        if (err < 0)
            break;
    }
    unlock(&_lock);
    return err;
}

- (void)locationManager:(CLLocationManager *)manager didChangeAuthorizationStatus:(CLAuthorizationStatus)status {
    if (status == kCLAuthorizationStatusAuthorizedAlways || status == kCLAuthorizationStatusAuthorizedWhenInUse) {
        NSLog(@"got auth, starting updates");
        [manager startUpdatingLocation];
    }
}

@end

@interface LocationFile : NSObject {
    NSData *buffer;
    size_t bufferOffset;
}

@property LocationTracker *tracker;

- (ssize_t)readIntoBuffer:(void *)buf size:(size_t)size;

@end

@implementation LocationFile

- (instancetype)init {
    if (self = [super init]) {
        self.tracker = [LocationTracker instance];
    }
    return self;
}

- (int)waitForUpdate {
    if (buffer != nil)
        return 0;
    int err = [self.tracker waitForUpdate];
    if (err < 0)
        return err;
    CLLocation *location = self.tracker.latest;
    NSString *output = [NSString stringWithFormat:@"%+f,%+f\n", location.coordinate.latitude, location.coordinate.longitude];
    buffer = [output dataUsingEncoding:NSUTF8StringEncoding];
    bufferOffset = 0;
    return 0;
}

- (ssize_t)readIntoBuffer:(void *)buf size:(size_t)size {
    @synchronized (self) {
        int err = [self waitForUpdate];
        if (err < 0)
            return err;
        size_t remaining = buffer.length - bufferOffset;
        if (size > remaining)
            size = remaining;
        [buffer getBytes:buf range:NSMakeRange(bufferOffset, size)];
        bufferOffset += size;
        if (bufferOffset == buffer.length)
            buffer = nil;
        return size;
    }
}

@end

static int location_open(int major, int minor, struct fd *fd) {
    fd->data = (void *) CFBridgingRetain([LocationFile new]);
    return 0;
}

static int location_close(struct fd *fd) {
    CFBridgingRelease(fd->data);
    return 0;
}

static ssize_t location_read(struct fd *fd, void *buf, size_t size) {
    LocationFile *file = (__bridge LocationFile *) fd->data;
    return [file readIntoBuffer:buf size:size];
}

const struct dev_ops location_dev = {
    .open = location_open,
    .fd.close = location_close,
    .fd.read = location_read,
};
