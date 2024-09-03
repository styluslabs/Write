#import <UIKit/UIKit.h>
#import <StoreKit/StoreKit.h>
#include "ioshelper.h"
#include "ugui/svggui_platform.h"  // for SDL and pen ids

static const char* kUTTypeJPEG = "public.jpeg";
// #import <MobileCoreServices/MobileCoreServices.h>

// w/ UIDocumentBrowser, SDL VC is no longer root (but still must be be used to present, e.g. image picker)
static UIViewController* sdlViewController = nil;
static CFRunLoopRef mainRunLoop = NULL;

void iosPumpEventsBlocking(void)
{
  if(!mainRunLoop)
    mainRunLoop = CFRunLoopGetCurrent();
  CFRunLoopRunInMode(kCFRunLoopDefaultMode, 100, TRUE);
  //int result;
  //do {
  //  result = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 10, TRUE);  // 10 seconds
  //} while (result == kCFRunLoopRunTimedOut);
}

void iosWakeEventLoop(void)
{
  if(mainRunLoop)
    CFRunLoopStop(mainRunLoop);
}

static void uiImagePicked(UIImage* image, BOOL isJpeg, int fromclip)
{
  //NSLog(isJpeg ? @"Got JPEG image" : @"Got PNG image");
  // getting raw bytes of UIImage is messy (via CGDataProvider via CGImage) - this is slower but seems safer
  NSData* data = isJpeg ? UIImageJPEGRepresentation(image, 0.8) : UIImagePNGRepresentation(image);
  imagePicked(data.bytes, data.length, fromclip);
}

@interface ImagePicker : UIImagePickerController
@end

@interface ImagePicker () <UINavigationControllerDelegate, UIImagePickerControllerDelegate>
@end

@implementation ImagePicker

- (void)imagePickerController:(UIImagePickerController *)picker didFinishPickingMediaWithInfo:(NSDictionary *)info
{
  //for(id key in info) NSLog(@"key=%@ value=%@", key, [info objectForKey:key]);
  UIImage* image = info[UIImagePickerControllerOriginalImage];  //UIImagePickerControllerEditedImage];
  [picker dismissViewControllerAnimated:YES completion:nil];
  // UIImagePickerControllerMediaType is public.image, so not useful for determined type
  NSURL* url = info[UIImagePickerControllerImageURL];
  BOOL isPng = url && [url.pathExtension caseInsensitiveCompare:@"png"] == NSOrderedSame;
  uiImagePicked(image, !isPng, 0);
}

@end

void showImagePicker(void)
{
  //UIViewController* viewController = UIApplication.sharedApplication.delegate.window.rootViewController;
  ImagePicker* picker = [[ImagePicker alloc] init];
  picker.delegate = picker;
  picker.allowsEditing = NO;
  picker.sourceType = UIImagePickerControllerSourceTypePhotoLibrary;  //UIImagePickerControllerSourceTypeCamera;
  // FormSheet and Popover are too small; PageSheet fills screen in portrait orientation; default seems to fill
  //  the screen in both orientations
  picker.modalPresentationStyle = UIModalPresentationPageSheet;
  [sdlViewController presentViewController:picker animated:YES completion:nil];
}

// clipboard state
// it seems UIPasteboardChangedNotification stopped working in iOS 10

int iosClipboardChangeCount(void)
{
  return [UIPasteboard generalPasteboard].changeCount;
}

int iosGetClipboardImage(void)
{
  if(![UIPasteboard generalPasteboard].hasImages)
    return 0;
  UIImage* image = [UIPasteboard generalPasteboard].images.firstObject;
  uiImagePicked(image, [[UIPasteboard generalPasteboard] containsPasteboardTypes:@[@(kUTTypeJPEG)]], 1);
  return 1;
}

// open URL

void iosOpenUrl(const char* url)
{
  [UIApplication.sharedApplication openURL:[NSURL URLWithString:@(url)] options:@{} completionHandler:nil];
}

// request review - note that -framework StoreKit has been manually added to link options for Xcode project
//  because adding to list of libraries forces in-app purchase which fails if not in provisioning profile

void iosRequestReview(void)
{
  // should always appear in dev build if in-app reviews enabled in settings and device has internet access
  [SKStoreReviewController requestReview];
}

// in-app purchase
// ref: https://stackoverflow.com/questions/19556336/how-do-you-add-an-in-app-purchase-to-an-ios-application
#ifdef SCRIBBLE_IAP
static int proIAP = -1;

void showErrorAlert(NSString* alertmsg)
{
  NSString* alerttitle = @(_("Error"));
  UIAlertController* alertController = [UIAlertController alertControllerWithTitle:alerttitle message:alertmsg preferredStyle:UIAlertControllerStyleAlert];
  UIAlertAction* cancelAction = [UIAlertAction actionWithTitle:@(_("OK")) style:UIAlertActionStyleCancel handler:nil];
  [alertController addAction:cancelAction];
  dispatch_async(dispatch_get_main_queue(), ^{
    [sdlViewController presentViewController:alertController animated:YES completion:nil];
  });
}

@interface IAPHandler : NSObject
@end

@interface IAPHandler () <SKProductsRequestDelegate, SKPaymentTransactionObserver>
@end

@implementation IAPHandler

#define kProIAPid @"com.styluslabs.write3a.iap"

- (void)productsRequest:(SKProductsRequest *)request didReceiveResponse:(SKProductsResponse *)response
{
  if([response.products count] > 0) {
    SKProduct* product = [response.products objectAtIndex:0];  //NSLog(@"Products Available!");
    SKPayment* payment = [SKPayment paymentWithProduct:product];
    [[SKPaymentQueue defaultQueue] addPayment:payment];
  }
  else
    showErrorAlert(@(_("Unable to connect, please try again later")));
}

- (void) paymentQueueRestoreCompletedTransactionsFinished:(SKPaymentQueue *)queue
{
  if(!proIAP)
    showErrorAlert(@(_("No existing purchases found.")));
}

- (void)paymentQueue:(SKPaymentQueue *)queue restoreCompletedTransactionsFailedWithError:(NSError *)error
{
  if(!proIAP && error.code != SKErrorPaymentCancelled)
    showErrorAlert(@(_("Error restoring purchase, please try again later.")));
}

- (void)paymentQueue:(SKPaymentQueue *)queue updatedTransactions:(NSArray *)transactions
{
  for(SKPaymentTransaction* transaction in transactions) {
    NSLog(@"Transaction: %p state: %ld", transaction, (long)transaction.transactionState);
    switch(transaction.transactionState) {
    case SKPaymentTransactionStatePurchased:  // 1
    case SKPaymentTransactionStateRestored:  // 3
      // check transaction.payment.productIdentifier if multiple IAPs
      proIAP = 1;
      iapCompleted();
      [[NSUserDefaults standardUserDefaults] setBool:proIAP forKey:@"proIAP"];
      //[[NSUserDefaults standardUserDefaults] synchronize];  -- not needed as of iOS 12
      [[SKPaymentQueue defaultQueue] finishTransaction:transaction];
      break;
    case SKPaymentTransactionStateFailed:  // 2
      if(transaction.error.code != SKErrorPaymentCancelled)
        showErrorAlert(transaction.error.localizedDescription);  //[NSString stringWithFormat:@(_("Error %d completing purchase, please try again later.")), transaction.error.code]);
      [[SKPaymentQueue defaultQueue] finishTransaction:transaction];
      break;
    default:  //SKPaymentTransactionStatePurchasing = 0, SKPaymentTransactionStateDeferred = 4
      break;
    }
  }
}

@end  // IAPHandler

static IAPHandler* iapHandler = nil;

// reciept validation is more secure than saving flag to user defaults, but looks too complicated
int iosIsPaid(void)
{
  if(proIAP < 0)
    proIAP = [[NSUserDefaults standardUserDefaults] boolForKey:@"proIAP"];
  return proIAP;
}

void iosRequestIAP(void)
{
  if(!iapHandler)  // iapHandler not created if already paid, and upgrade button not shown!
    return;
  if(![SKPaymentQueue canMakePayments]) {
    // seems even restoring purchases is prevented if IAP is disabled (in Screen Time)
    showErrorAlert(@(_("In-app purchases have been disabled on this device.")));
    return;
  }
  NSString* alerttitle = @(_("Upgrade Write"));
  NSString* alertmsg = @(_("Make a one-time purchase to permanently remove page watermark."));
  UIAlertController* alertController = [UIAlertController alertControllerWithTitle:alerttitle message:alertmsg preferredStyle:UIAlertControllerStyleAlert];
  if([SKPaymentQueue canMakePayments]) {
    UIAlertAction* purchaseAction = [UIAlertAction actionWithTitle:@(_("Purchase")) style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
      SKProductsRequest* productsRequest = [[SKProductsRequest alloc] initWithProductIdentifiers:[NSSet setWithObject:kProIAPid]];
      productsRequest.delegate = iapHandler;
      [productsRequest start];
    }];
    [alertController addAction:purchaseAction];
  }
  UIAlertAction* restoreAction = [UIAlertAction actionWithTitle:@(_("Restore")) style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
    [[SKPaymentQueue defaultQueue] restoreCompletedTransactions];
  }];
  [alertController addAction:restoreAction];
  UIAlertAction* cancelAction = [UIAlertAction actionWithTitle:@(_("Cancel")) style:UIAlertActionStyleCancel handler:nil];
  [alertController addAction:cancelAction];
  [sdlViewController presentViewController:alertController animated:YES completion:nil];
}

#endif //SCRIBBLE_IAP

// locale

const char* iosGetLocale(void)
{
  static char lstr[6];
  NSString* lang = [[NSLocale preferredLanguages] firstObject];
  if([lang hasPrefix:@"zh-Hans"]) lang = @"zh-CN";
  else if([lang hasPrefix:@"zh-Hant"]) lang = @"zh-TW";
  strncpy(&lstr[0], lang.UTF8String, 5);
  lstr[5] = '\0';
  return &lstr[0];
}

// share file

// NOTE: Save to Photos (but not other options) dismisses view due to bug in iOS 13
//  see https://stackoverflow.com/questions/56903030 for workarounds
void iosSendFile(const char* filename)  //, const char* mimetype, const char* title)
{
  NSURL* fileurl = [NSURL fileURLWithPath:@(filename)];
  UIActivityViewController* sendActivity =
      [[UIActivityViewController alloc] initWithActivityItems:@[fileurl] applicationActivities:nil];

  // needed for iPad
  UIView* topView = sdlViewController.view;
  sendActivity.popoverPresentationController.sourceView = topView;
  // sourceRect is the anchor point for the popover
  sendActivity.popoverPresentationController.sourceRect = CGRectMake(topView.bounds.size.width/2, 0, 0, 0);

  [sdlViewController presentViewController:sendActivity animated:YES completion:nil];
}

// combined handler for drag and drop and Apple Pencil double tap

@interface InteractionHandler : NSObject
@end

@interface InteractionHandler () < UIDropInteractionDelegate, UIPencilInteractionDelegate>
@end

@implementation InteractionHandler

- (BOOL)dropInteraction:(UIDropInteraction *)interaction canHandleSession:(id<UIDropSession>)session
{
  return [session canLoadObjectsOfClass:[UIImage class]];
}

- (UIDropProposal *)dropInteraction:(UIDropInteraction *)interaction sessionDidUpdate:(id<UIDropSession>)session
{
  return [[UIDropProposal alloc] initWithDropOperation:UIDropOperationCopy];
}

- (void)dropInteraction:(UIDropInteraction *)interaction performDrop:(id<UIDropSession>)session
{
  // CGPoint dropLocation = [session locationInView:sdlViewController];
  [session loadObjectsOfClass:[UIImage class] completion:^(NSArray<__kindof id<NSItemProviderReading>> * _Nonnull objects) {
    UIImage *image = (UIImage*)[objects firstObject];
    if(image)
      uiImagePicked(image, [session hasItemsConformingToTypeIdentifiers:@[@(kUTTypeJPEG)]], 0);
  }];
}

- (void)pencilInteractionDidTap:(UIPencilInteraction *)interaction  API_AVAILABLE(ios(12.1))
{
  //NSLog(@"Received pencilInteractionDidTap");
  pencilBarrelTap();
}

@end

// safe area insets

int iosSafeAreaInsets(float* top, float* bottom)
{
  if (UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad)
    return 0;

  UIWindow *window = UIApplication.sharedApplication.keyWindow;
  CGFloat topInset = window.safeAreaInsets.top;
  CGFloat bottomInset = window.safeAreaInsets.bottom;
  if(top) *top = (float)topInset;
  if(bottom) *bottom = (float)bottomInset;
  return 1;
}

// status bar color - for use with UIViewControllerBasedStatusBarAppearance = true in Info.plist

#import "SDL/src/video/uikit/SDL_uikitviewcontroller.h"
//@interface SDL_uikitviewcontroller(StatusBar) @end  ... doesn't seem to be necessary

@implementation SDL_uikitviewcontroller(StatusBar)

- (UIStatusBarStyle)preferredStatusBarStyle
{
  return UIStatusBarStyleLightContent;
}

@end

// touch input

#import "SDL/src/video/uikit/SDL_uikitview.h"

@implementation SDL_uikitview(Pencil)
//static float prevForce = -1;

- (void)sendTouchEvent:(UITouch *)touch ofType:(int)eventType forFinger:(size_t)fingerId
{
  SDL_TouchID touchId = touch.type == UITouchTypeStylus ? PenPointerPen : 1;
  CGPoint pos = [touch preciseLocationInView:self];
  // without % INT_MAX, ts gets clamped to INT_MAX and kinetic scroll breaks after ~24 days
  int ts = (int)((long long)([touch timestamp]*1000.0 + 0.5) % INT_MAX);
  // touch.force / touch.maximumPossibleForce? - force is normalized so that 1.0 is normal force but can
  //  be greater than 1, while StrokeBuilder clamps pressure to 1
  // ... for now, just divide by 2 since 0.5 is typical pressure on other platforms
  float pressure = (touchId == PenPointerPen || touch.force > 0) ? (float)touch.force/2 : 1.0f;
  // we'll use diameter instead of radius (closer to Windows, Android)
  float w = 2*touch.majorRadius;

  SDL_Event event = {0};
  event.type = eventType;
  event.tfinger.timestamp = ts;  //SDL_GetTicks();  // normally done by SDL_PushEvent()
  event.tfinger.touchId = touchId;
  event.tfinger.fingerId = touchId == PenPointerPen ? SDL_BUTTON_LMASK : (SDL_FingerID)fingerId;
  event.tfinger.x = pos.x;
  event.tfinger.y = pos.y;
  // size of touch point
  event.tfinger.dx = w;
  event.tfinger.dy = w - 2*touch.majorRadiusTolerance;  // for now, Write just uses larger axis, so this has no effect
  event.tfinger.pressure = pressure;
  // PeepEvents bypasses gesture recognizer and event filters
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);  //SDL_PushEvent(&event);
  //const char* evname = eventType == SDL_FINGERDOWN ? "SDL_FINGERDOWN" : (eventType == SDL_FINGERUP ? "SDL_FINGERUP"
  //    : (eventType == SVGGUI_FINGERCANCEL ? "SVGGUI_FINGERCANCEL" : "SDL_FINGERMOTION"));
  //NSLog(@"%s touch: %f, %f; force %f; radius %f; time %d", evname, pos.x, pos.y, touch.force, touch.majorRadius, ts);
}

//- (void)touchesEstimatedPropertiesUpdated:(NSSet *)touches
//{
//  //if(waitForForceUpdate > 1)
//  //  waitForForceUpdate = 1;
//}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event
{
  for (UITouch* touch in touches)
    [self sendTouchEvent:touch ofType:SDL_FINGERDOWN forFinger:(size_t)touch];
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event
{
  for (UITouch* touch in touches)
    [self sendTouchEvent:touch ofType:SDL_FINGERUP forFinger:(size_t)touch];
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event
{
  for (UITouch* touch in touches)
    [self sendTouchEvent:touch ofType:SVGGUI_FINGERCANCEL forFinger:(size_t)touch];
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event
{
  for (UITouch* touch in touches) {
    NSArray<UITouch*>* cTouches = [event coalescedTouchesForTouch:touch];
    for (UITouch* cTouch in cTouches)
      [self sendTouchEvent:cTouch ofType:SDL_FINGERMOTION forFinger:(size_t)touch];
  }
}

// TODO: try this once we upgrade xcode for iOS 13.4+
// ref: https://github.com/utmapp/UTM/issues/223 and linked PR
// UIPanGestureRecognizer is apparently the only way to get magic keyboard touchpad scroll events
// alternative approach would be to send locationInView as mouse motion events, and force mouse mode to pan
/*- (IBAction)gestureScroll:(UIPanGestureRecognizer *)sender API_AVAILABLE(ios(13.4))
{
  //CGPoint pos = [sender locationInView:sender.view];
  //CGPoint velocity = [sender velocityInView:sender.view];
  static CGPoint dr0;
  CGPoint dr = [sender translationInView:sender.view];
  if (sender.state == UIGestureRecognizerStateBegan) dr0 = {0,0};

  SDL_Event event = {0};
  event.type = SDL_MOUSEWHEEL;
  event.wheel.timestamp = SDL_GetTicks();  // gesture recognizer doesn't provide timestamp?
  event.wheel.windowID = 0;
  event.wheel.which = 0;  //SDL_TOUCH_MOUSEID;
  event.wheel.x = dr.x - dr0.x;
  event.wheel.y = dr.y - dr0.y;
  event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
  dr0 = dr;
}*/

@end

// WriteDocument : UIDocument

@interface WriteDocument : UIDocument
{
  void* buffer;
  size_t buffsize;
}

//@property NSData* saveData;
@property UIImage* thumbnail;
@property long docTag;

@end

@implementation WriteDocument

// Lifecycle for WriteDocument buffer (UIDocStream holds copy of buffer address)
// New doc: WriteDocument creates and fills buffer; WriteDocument frees buffer when destroyed (dealloc)
//  - WriteDocument's buffer ptr via ioSaveDocument if realloced during Document::save
// Reload:
// - same contents: ignored, buffer not replaced
// - different contents, document unmodified in Write: UIDocStream.uiDocument cleared before UIDocStream
//  replaced, so iosCloseDocument not called and old buffer freed
// - different contents, document modified in Write: new buffer is freed immediately; old buffer restored on
//  WriteDocument (via call to ioSaveDocument  w/ len = 0) since it may be needed to load remaining pages;
//  freed when WriteDocument/UIDocStream is replaced by Save As

// safe if this is called on same thread as Document::save(), so buffer can't change during init of NSData
- (id)contentsForType:(NSString*)typeName error:(NSError **)errorPtr
{
  return buffsize > 0 ? [NSData dataWithBytes:buffer length:buffsize] : nil;
}

- (BOOL)loadFromContents:(id)contents ofType:(NSString *)typeName error:(NSError **)errorPtr
{
  NSData* data = (NSData*)contents;

  if(buffer && buffsize == data.length && memcmp(buffer, data.bytes, buffsize) == 0) {
    //NSLog(@"Got duplicate loadFromContents");
    return YES;
  }
  // in iosUpdateDocMode, UIDocStream being replaced will free the current `buffer`
  buffsize = data.length;
  size_t reserve = buffsize + (4 << 20);
  buffer = malloc(reserve);
  memcpy(buffer, data.bytes, buffsize);

  SDL_Event event = {0};
  event.type = SDL_DROPFILE;
  event.user.timestamp = SDL_GetTicks();  // normally done by SDL_PushEvent()
  event.user.data1 = loadDocumentContents(buffer, buffsize, reserve, self.fileURL.path.UTF8String, (__bridge void*)self);
  event.user.data2 = (void*)self.docTag;
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);  //SDL_PushEvent(&event);

  // if loadFromContents is called again, that means doc was updated externally (w/o conflict)
  self.docTag = iosUpdateDocMode;

  return YES;
}

- (NSDictionary *)fileAttributesToWriteToURL:(NSURL *)url
    forSaveOperation:(UIDocumentSaveOperation)saveOperation error:(NSError * _Nullable *)outError
{
  // NSURLHasHiddenExtensionKey:YES is the default value (returned by the method this overrides)
  if(!self.thumbnail)  // passing nil for thumbnail causes crash, so don't do it!
    return @{NSURLHasHiddenExtensionKey: @(YES)};
  return @{NSURLHasHiddenExtensionKey: @(YES),
      NSURLThumbnailDictionaryKey: @{NSThumbnail1024x1024SizeKey: self.thumbnail}};
}

- (void)setBuffer:(void*)data length:(size_t)len
{
  buffer = data;
  buffsize = len;
}

- (void)dealloc
{
  free(buffer);
}

@end

// UIDocumentBrowserViewController - DocumentBrowser

@interface DocumentBrowser : UIDocumentBrowserViewController

- (void)presentDocumentAtURL:(NSURL *)documentURL;

@end

@interface DocumentBrowser () <UIDocumentBrowserViewControllerDelegate>
@end

@implementation DocumentBrowser

- (void)viewDidLoad
{
  [super viewDidLoad];
  self.delegate = self;
  self.allowsDocumentCreation = YES;
  self.allowsPickingMultipleItems = NO;

  // If we force UIUserInferfaceStyle = Dark in Info,plist, set this for iOS < 13
  //self.browserUserInterfaceStyle = UIDocumentBrowserUserInterfaceStyleDark;
  // self.view.tintColor = [UIColor whiteColor];
  // Specify the allowed content types of your application via the Info.plist.
  // Do any additional setup after loading the view, typically from a nib.
}

#pragma mark UIDocumentBrowserViewControllerDelegate

- (void)documentBrowser:(UIDocumentBrowserViewController *)controller didRequestDocumentCreationWithHandler:(void (^)(NSURL * _Nullable, UIDocumentBrowserImportMode))importHandler
{
  UIAlertController *alertController = [UIAlertController alertControllerWithTitle:@(_("Enter Document Name")) message:@"" preferredStyle:UIAlertControllerStyleAlert];
  [alertController addTextFieldWithConfigurationHandler:^(UITextField * _Nonnull textField) {
    NSDate *date = [NSDate date];
    time_t time = [date timeIntervalSince1970];
    struct tm timeStruct;
    localtime_r(&time, &timeStruct);
    char buffer[80];
    strftime(buffer, 80, getCfgString("newDocTitleFmt", "%b %e %Hh%M"), &timeStruct);
    NSString *dateStr = [NSString stringWithCString:buffer encoding:NSASCIIStringEncoding];
    textField.text = dateStr;
  }];
  UIAlertAction *confirmAction = [UIAlertAction actionWithTitle:@(_("OK")) style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
    NSString* rawtitle = [[alertController textFields][0] text];
    NSString* title = [rawtitle stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    NSString* filePath = [NSTemporaryDirectory() stringByAppendingPathComponent:title];
    if(!([title hasSuffix:@".svgz"] || [title hasSuffix:@".svg"] || [title hasSuffix:@".htm"] || [title hasSuffix:@".html"]))
      filePath = [filePath stringByAppendingPathExtension:@(getCfgString("docFileExt", "svgz"))];
    NSURL* url = [NSURL fileURLWithPath:filePath];
    NSData* data = [@"" dataUsingEncoding:NSUTF8StringEncoding];
    if([title length] > 0 && url && [data writeToURL:url atomically:NO] == YES)
      importHandler(url, UIDocumentBrowserImportModeMove);
    else
      importHandler(nil, UIDocumentBrowserImportModeNone);
  }];
  [alertController addAction:confirmAction];
  UIAlertAction *cancelAction = [UIAlertAction actionWithTitle:@(_("Cancel")) style:UIAlertActionStyleCancel handler:^(UIAlertAction * _Nonnull action) {
    importHandler(nil, UIDocumentBrowserImportModeNone);
  }];
  [alertController addAction:cancelAction];
  [self presentViewController:alertController animated:YES completion:^ {
    [[alertController textFields][0] selectAll:nil];
  }];
}

- (void)documentBrowser:(UIDocumentBrowserViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)documentURLs {
  NSURL *sourceURL = documentURLs.firstObject;
  if (sourceURL)
    [self presentDocumentAtURL:sourceURL];
}

// for iOS 11
- (void)documentBrowser:(UIDocumentBrowserViewController *)controller didPickDocumentURLs:(NSArray<NSURL *> *)documentURLs {
  NSURL *sourceURL = documentURLs.firstObject;
  if (sourceURL)
    [self presentDocumentAtURL:sourceURL];
}

- (void)documentBrowser:(UIDocumentBrowserViewController *)controller didImportDocumentAtURL:(NSURL *)sourceURL toDestinationURL:(NSURL *)destinationURL {
  // Present the Document View Controller for the newly created document
  [self presentDocumentAtURL:destinationURL];
}

- (void)documentBrowser:(UIDocumentBrowserViewController *)controller failedToImportDocumentAtURL:(NSURL *)documentURL error:(NSError * _Nullable)error {
  // Make sure to handle the failed import appropriately, e.g., by presenting an error message to the user.
}

- (void)presentDocumentAtURL:(NSURL *)documentURL
{
  // since we don't want doc browser to show html files, we'll open multi-file docs via any page file
  // ... but we can only do this if file is in app sandbox (hence the isReadableFileAtPath check)
  NSString* path = documentURL.path;
  if([path hasSuffix:@".svg"]) {
    path = [path substringToIndex:[path length]-7];  // remove "XXX.svg"
    if([path hasSuffix:@"_page"]) {
      path = [[path substringToIndex:[path length]-5] stringByAppendingString:@".html"];  // remove "_page"
      if([[NSFileManager defaultManager] isReadableFileAtPath:path])
        documentURL = [NSURL fileURLWithPath:path];
    }
  }

  WriteDocument* doc = [[WriteDocument alloc] initWithFileURL:documentURL];
  doc.docTag = iosOpenDocMode;
  [doc openWithCompletionHandler:nil];
  void* retain_doc = (__bridge_retained void*)doc;
  [self presentViewController:sdlViewController animated:YES completion:nil];
}

@end

// opening "dropped" files
#import "SDL/src/video/uikit/SDL_uikitappdelegate.h"

@interface SDLUIKitDelegate(Drop)

- (BOOL)application:(UIApplication *)app openURL:(NSURL *)url options:(NSDictionary<UIApplicationOpenURLOptionsKey,id> *)options;

@end

@implementation SDLUIKitDelegate(Drop)

- (BOOL)application:(UIApplication *)app openURL:(NSURL *)url options:(NSDictionary<UIApplicationOpenURLOptionsKey,id> *)options
{
  if(!url.isFileURL)
    return NO;
  // What is the point of this? Can't we just create UIDocument directly?
  DocumentBrowser* docBrowser = (DocumentBrowser*)self.window.rootViewController;
  if(docBrowser.presentedViewController)
    [docBrowser.presentedViewController dismissViewControllerAnimated:NO completion:nil];
  [docBrowser revealDocumentAtURL:url importIfNeeded:YES completion:^(NSURL* _Nullable revealedURL, NSError* _Nullable error) {
    // calling (our method) presentDocumentAtURL directly works for opening directly and opening copy, but in
    //  the latter case, there is no way for user to reopen the copy after closing; w/ revealDocumentAtURL,
    //  we must use importIfNeeded when opening copy, but this calls didImportDocumentAtURL which calls
    //  presentDocumentAtURL (needed for normal create document workflow), so here we must check to see if
    //  document is already being presented (true for open copy, false otherwise)
    if(revealedURL && !error && !docBrowser.presentedViewController)
      [docBrowser presentDocumentAtURL:revealedURL];
  }];
  return YES;
}

@end

// Document picker for insert doc
@interface DocumentPicker : UIDocumentPickerViewController

@property long docTag;

@end

@interface DocumentPicker () <UIDocumentPickerDelegate>
@end

@implementation DocumentPicker

- (void)documentPicker:(UIDocumentPickerViewController*)picker didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
  WriteDocument* document = [[WriteDocument alloc] initWithFileURL:urls.firstObject];
  document.docTag = self.docTag;  // insert doc
  [document openWithCompletionHandler:nil];
  void* retain_document = (__bridge_retained void*)document;
  //[picker dismissViewControllerAnimated:YES completion:nil];
}

@end

// Document picker for conflict handling
@interface ConflictDocPicker : UIDocumentPickerViewController

@property NSFileVersion* fileVersion;
@property NSURL* tempURL;

@end

@interface ConflictDocPicker () <UIDocumentPickerDelegate>
@end

@implementation ConflictDocPicker

- (void)documentPicker:(UIDocumentPickerViewController*)picker didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
  [self.fileVersion removeAndReturnError:nil];
  self.fileVersion.resolved = YES;
  [[NSFileManager defaultManager] removeItemAtURL:self.tempURL error:nil];
  //NSLog(@"ConflictDocPicker accepted");
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)picker
{
  [[NSFileManager defaultManager] removeItemAtURL:self.tempURL error:nil];
}

@end

void resolveDocumentConflict(UIViewController* viewController, NSURL* url)
{
  NSString* basename = [url.lastPathComponent stringByDeletingPathExtension];
  NSString* extension = url.pathExtension;
  //NSFileVersion* currentVersion = [NSFileVersion currentVersionOfItemAtURL:url];
  NSArray* otherVersions = [NSFileVersion otherVersionsOfItemAtURL:url];
  __block int versionCount = 1;
  for(NSFileVersion* fileVersion in otherVersions) {
    if(!fileVersion.conflict)
      continue;
    NSString* mtime = [NSDateFormatter localizedStringFromDate:fileVersion.modificationDate
        dateStyle:NSDateFormatterShortStyle timeStyle:NSDateFormatterShortStyle];
    NSString* alerttitle = @(_("Document Conflict"));
    NSString* alertmsg = [NSString stringWithFormat:@(_("Please choose a new location for version of %@ modified %@ by %@")),
        basename, mtime, fileVersion.localizedNameOfSavingComputer];
    UIAlertController *alertController = [UIAlertController alertControllerWithTitle:alerttitle message:alertmsg preferredStyle:UIAlertControllerStyleAlert];
    UIAlertAction *confirmAction = [UIAlertAction actionWithTitle:@(_("OK")) style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
      NSString* fileName = [NSString stringWithFormat:@"%@ %d.%@", basename, ++versionCount, extension];
      NSString* filePath = [NSTemporaryDirectory() stringByAppendingPathComponent:fileName];
      NSURL* tempURL = [NSURL fileURLWithPath:filePath];
      [[NSFileManager defaultManager] removeItemAtURL:tempURL error:nil];
      // TODO: we probably are supposed to use a file coordinator
      if([[NSFileManager defaultManager] copyItemAtURL:fileVersion.URL toURL:tempURL error:nil] == YES) {
        ConflictDocPicker *docPicker = [[ConflictDocPicker alloc] initWithURL:tempURL inMode:UIDocumentPickerModeMoveToService];
        docPicker.fileVersion = fileVersion;
        docPicker.tempURL = tempURL;
        docPicker.delegate = docPicker;
        docPicker.modalPresentationStyle = UIModalPresentationFormSheet;
        [viewController presentViewController:docPicker animated:YES completion:nil];
      }
      else
        NSLog(@"Copying conflict version failed");
    }];
    [alertController addAction:confirmAction];
    [viewController presentViewController:alertController animated:YES completion:nil];
  }
}

// interface to Write

// delegate refs are weak, so we need a strong ref to InteractionHandler
static InteractionHandler* interactionHandler = nil;

void initDocumentBrowser(const char* bkmkBase64)
{
  DocumentBrowser* browser = [[DocumentBrowser alloc] initForOpeningFilesWithContentTypes:@[@"public.svg-image"]];
  UIWindow* sdlUIWindow = UIApplication.sharedApplication.delegate.window;
  sdlViewController = sdlUIWindow.rootViewController;
  sdlUIWindow.rootViewController = nil;
  sdlUIWindow.rootViewController = browser;
  // fullscreen was the default prior to iOS 13
  sdlViewController.modalPresentationStyle = UIModalPresentationFullScreen;
  // "Create Document"
  if (@available(iOS 13.0, *))
    browser.localizedCreateDocumentActionTitle = @(_("Create Document"));

  // drag and drop; Apple Pencil 2 double tap
  interactionHandler = [[InteractionHandler alloc] init];
  UIDropInteraction* dropInteraction = [[UIDropInteraction alloc] initWithDelegate:interactionHandler];
  [sdlViewController.view addInteraction:dropInteraction];
  if (@available(iOS 12.1, *)) {
    UIPencilInteraction* pencilInteraction = [[UIPencilInteraction alloc] init];
    pencilInteraction.delegate = interactionHandler;
    [sdlViewController.view addInteraction:pencilInteraction];
  }

  // in-app purchase
#ifdef SCRIBBLE_IAP
  if(!iosIsPaid()) {
    iapHandler = [[IAPHandler alloc] init];
    [[SKPaymentQueue defaultQueue] addTransactionObserver:iapHandler];
  }
#endif

  // magic keyboard scrolling
  /*if (@available(iOS 13.4, *)) {
    UIPanGestureRecognizer *scroll = [[UIPanGestureRecognizer alloc]
        initWithTarget:sdlViewController.view action:@selector(gestureScroll:)];
    scroll.allowedScrollTypesMask = UIScrollTypeMaskAll;
    scroll.minimumNumberOfTouches = 0;
    scroll.maximumNumberOfTouches = 0;
    scroll.allowedTouchTypes = @[];  // disable interception of touch events
    [sdlViewController.view addGestureRecognizer:scroll];
  }*/

  // conflict resolution
  [[NSNotificationCenter defaultCenter] addObserverForName:UIDocumentStateChangedNotification object:nil queue:nil
      usingBlock:^(NSNotification* note) {
    UIDocument* doc = note.object;
    // documentState is bitmap - if only conflict bit is set, it means document is open normally
    if(doc.documentState == UIDocumentStateInConflict) {
      UIViewController* pvc = browser.presentedViewController;
      resolveDocumentConflict(pvc ? pvc : browser, doc.fileURL);
    }
  }];

  // reopening previous doc
  if(bkmkBase64 && bkmkBase64[0]) {
    // "!" is passed if doc already opened (used for help doc on first run)
    if(strcmp(bkmkBase64, "!") == 0)
      [browser presentViewController:sdlViewController animated:YES completion:nil];
    else {
      // bookmark will be updated when the document is opened, so we ignore bookmarkDataIsStale
      NSData* bookmark = [[NSData alloc]
          initWithBase64EncodedString:@(bkmkBase64) options:NSDataBase64DecodingIgnoreUnknownCharacters];
      NSURL* url = [NSURL URLByResolvingBookmarkData:bookmark options:0
          relativeToURL:nil bookmarkDataIsStale:nil error:nil];
      if(url)
        [browser presentDocumentAtURL:url];
    }
  }
}

void showDocumentBrowser()
{
  [sdlViewController dismissViewControllerAnimated:YES completion:nil];
}

// close document and show the document browser by dismissing the SDL view controller
void iosCloseDocument(void* _doc)
{
  WriteDocument* doc = (__bridge_transfer WriteDocument*)_doc;
  [doc closeWithCompletionHandler:nil];
}

void iosSaveDocument(void* _doc, void* data, int len)
{
  WriteDocument* doc = (__bridge WriteDocument*)_doc;
  [doc setBuffer:data length:len];
  if(len > 0)
    [doc updateChangeCount:UIDocumentChangeDone];
}

void iosSetDocThumbnail(void* _doc, void* data, int width, int height)
{
  WriteDocument* doc = (__bridge WriteDocument*)_doc;
  CFDataRef dataRef = CFDataCreate(NULL, data, width*height*4);
  CGDataProviderRef providerRef = CGDataProviderCreateWithCFData(dataRef);
  CGColorSpaceRef colorSpaceRef = CGColorSpaceCreateDeviceRGB();
  CGBitmapInfo flags = kCGBitmapByteOrderDefault | kCGImageAlphaLast;
  CGImageRef imageRef = CGImageCreate(width, height, 8, 32, 4*width,
      colorSpaceRef, flags, providerRef, NULL, NO, kCGRenderingIntentDefault);
  doc.thumbnail = [UIImage imageWithCGImage:imageRef];
  CGImageRelease(imageRef);
  CGColorSpaceRelease(colorSpaceRef);
  CGDataProviderRelease(providerRef);
  CFRelease(dataRef);
}

char* iosGetSecuredBookmark(void* _doc)
{
  WriteDocument* doc = (__bridge WriteDocument*)_doc;
  NSData* bookmark = [doc.fileURL bookmarkDataWithOptions:0
      includingResourceValuesForKeys:nil relativeToURL:nil error:nil];
  if(!bookmark)
    return NULL;
  NSString* base64 = [bookmark base64EncodedStringWithOptions:0];
  char* str = malloc(base64.length + 1);
  strncpy(str, base64.UTF8String, base64.length + 1);
  return str;
}

void iosSaveAs(long mode)
{
  NSString* alerttitle = mode == iosConflictSaveMode ? @(_("Document Conflict")) : @(_("Enter Document Name"));
  NSString* alertmsg = mode == iosConflictSaveMode ?
      @(_("Enter new document name, then choose save location")) : @(_("then choose destination"));
  UIAlertController *alertController = [UIAlertController alertControllerWithTitle:alerttitle message:alertmsg preferredStyle:UIAlertControllerStyleAlert];
  [alertController addTextFieldWithConfigurationHandler:nil];
  UIAlertAction *confirmAction = [UIAlertAction actionWithTitle:@(_("OK")) style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
    NSString* rawtitle = [[alertController textFields][0] text];
    NSString* title = [rawtitle stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    NSString* filePath = [NSTemporaryDirectory() stringByAppendingPathComponent:title];
    NSString* defaultExt = mode == iosExportPdfMode ? @"pdf" : @"svgz";
    if(!([title hasSuffix:@".svgz"] || [title hasSuffix:@".svg"] || [title hasSuffix:@".htm"] || [title hasSuffix:@".html"] || [title hasSuffix:@".pdf"]))
      filePath = [filePath stringByAppendingPathExtension:defaultExt];
    NSURL* url = [NSURL fileURLWithPath:filePath];
    NSData* data = [@"" dataUsingEncoding:NSUTF8StringEncoding];
    if([title length] > 0 && url && [data writeToURL:url atomically:NO] == YES) {
      DocumentPicker *documentPicker = [[DocumentPicker alloc]
          initWithURL:url inMode:UIDocumentPickerModeMoveToService];
      documentPicker.docTag = mode;
      documentPicker.delegate = documentPicker;
      documentPicker.modalPresentationStyle = UIModalPresentationFormSheet;
      [sdlViewController presentViewController:documentPicker animated:YES completion:nil];
    }
  }];
  [alertController addAction:confirmAction];
  if(mode != iosConflictSaveMode) {
    UIAlertAction *cancelAction = [UIAlertAction actionWithTitle:@(_("Cancel")) style:UIAlertActionStyleCancel handler:nil];
    [alertController addAction:cancelAction];
  }
  [sdlViewController presentViewController:alertController animated:YES completion:nil];
}

void iosPickDocument(long mode)
{
  DocumentPicker *documentPicker = [[DocumentPicker alloc]
      initWithDocumentTypes:@[@"public.svg-image"] inMode:UIDocumentPickerModeOpen];
  documentPicker.docTag = mode;
  documentPicker.delegate = documentPicker;
  documentPicker.modalPresentationStyle = UIModalPresentationFormSheet;
  [sdlViewController presentViewController:documentPicker animated:YES completion:nil];
}

//void iosMoveDocument(void* _doc, const char* newurl)
// Both NSFileManager moveItemAtURL and NSURL setResourceValue forKey:NSURLNameKey work for renaming
//  files in sandbox; use dispatch_async and NSFileCoordinator:
// see https://stackoverflow.com/questions/32430222/rename-document-without-closing-uidocument
// ... but nothing works for files outside sandbox; Apple apps (Keynote, etc.) appear to use private/hidden
//  UIDocumentBrowserVC method _renameDocumentAtURL:newName:completionBlock: - can't find any 3rd party
//  apps that support renaming
