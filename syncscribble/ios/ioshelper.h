#ifndef IOSHELPER_H
#define IOSHELPER_H

#ifdef __cplusplus
extern "C" {
#endif
extern void iosPumpEventsBlocking(void);
extern void iosWakeEventLoop(void);
extern void showImagePicker(void);
extern void iosOpenUrl(const char* url);
extern void iosRequestReview(void);
extern const char* iosGetLocale(void);
extern void iosSendFile(const char* filename);
extern int iosClipboardChangeCount(void);
extern int iosGetClipboardImage(void);
extern int iosSafeAreaInsets(float* top, float* bottom);

//extern void freeSecuredURL(void* data);
extern void initDocumentBrowser(const char* bkmkBase64);
extern void showDocumentBrowser(void);
extern void iosCloseDocument(void* _doc);
extern void iosSaveDocument(void* _doc, void* data, int len);
extern char* iosGetSecuredBookmark(void* _doc);
extern void iosSetDocThumbnail(void* _doc, void* data, int width, int height);
extern void iosSaveAs(long mode);
extern void iosPickDocument(long mode);

enum PickerMode_t { iosOpenDocMode = 0, iosUpdateDocMode, iosChooseDocMode, iosInsertDocMode,
    iosSaveAsMode, iosExportPdfMode, iosConflictSaveMode };

// functions expected to be available in Write
extern void imagePicked(const void* data, int len, int fromclip);
extern void pencilBarrelTap(void);
extern void* loadDocumentContents(void* data, size_t len, size_t reserve, const char* url, void* uidoc);
extern const char* getCfgString(const char* name, const char* dflt);
extern const char* _(const char*);  // translations

#ifdef SCRIBBLE_IAP
extern int iosIsPaid(void);
extern void iosRequestIAP(void);
extern void iapCompleted(void);  // expected in Write
#endif

#ifdef __cplusplus
}
#endif

#endif
