/*
 * Copyright (C) 2010, 2011 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#import "config.h"
#import "PDFViewController.h"

#import "DataReference.h"
#import "WKAPICast.h"
#import "WKView.h"
#import "WebPageGroup.h"
#import "WebPageProxy.h"
#import "WebPreferences.h"
#import <PDFKit/PDFKit.h>
#import <wtf/text/WTFString.h>

// Redeclarations of PDFKit notifications. We can't use the API since we use a weak link to the framework.
#define _webkit_PDFViewDisplayModeChangedNotification @"PDFViewDisplayModeChanged"
#define _webkit_PDFViewScaleChangedNotification @"PDFViewScaleChanged"
#define _webkit_PDFViewPageChangedNotification @"PDFViewChangedPage"

using namespace WebKit;

@class PDFDocument;
@class PDFView;

@interface PDFDocument (PDFDocumentDetails)
- (NSPrintOperation *)getPrintOperationForPrintInfo:(NSPrintInfo *)printInfo autoRotate:(BOOL)doRotate;
@end

extern "C" NSString *_NSPathForSystemFramework(NSString *framework);
    
@interface WKPDFView : NSView
{
    PDFViewController* _pdfViewController;

    RetainPtr<NSView> _pdfPreviewView;
    PDFView *_pdfView;
    BOOL _ignoreScaleAndDisplayModeAndPageNotifications;
    BOOL _willUpdatePreferencesSoon;
}

- (id)initWithFrame:(NSRect)frame PDFViewController:(PDFViewController*)pdfViewController;
- (void)invalidate;
- (PDFView *)pdfView;
- (void)setDocument:(PDFDocument *)pdfDocument;

- (void)_applyPDFPreferences;

@end

@implementation WKPDFView

- (id)initWithFrame:(NSRect)frame PDFViewController:(PDFViewController*)pdfViewController
{
    if ((self = [super initWithFrame:frame])) {
        _pdfViewController = pdfViewController;

        [self setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

        Class previewViewClass = PDFViewController::pdfPreviewViewClass();
        ASSERT(previewViewClass);

        _pdfPreviewView.adoptNS([[previewViewClass alloc] initWithFrame:frame]);
        [_pdfPreviewView.get() setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        [self addSubview:_pdfPreviewView.get()];

        _pdfView = [_pdfPreviewView.get() performSelector:@selector(pdfView)];
    }

    return self;
}

- (void)invalidate
{
    _pdfViewController = 0;
}

- (PDFView *)pdfView
{
    return _pdfView;
}

- (void)setDocument:(PDFDocument *)pdfDocument
{
    _ignoreScaleAndDisplayModeAndPageNotifications = YES;
    [_pdfView setDocument:pdfDocument];
    [self _applyPDFPreferences];
    _ignoreScaleAndDisplayModeAndPageNotifications = NO;
}

- (void)_applyPDFPreferences
{
    if (!_pdfViewController)
        return;

    WebPreferences *preferences = toImpl([_pdfViewController->wkView() pageRef])->pageGroup()->preferences();

    CGFloat scaleFactor = preferences->pdfScaleFactor();
    if (!scaleFactor)
        [_pdfView setAutoScales:YES];
    else {
        [_pdfView setAutoScales:NO];
        [_pdfView setScaleFactor:scaleFactor];
    }
    [_pdfView setDisplayMode:preferences->pdfDisplayMode()];
}

- (void)_updatePreferences:(id)ignored
{
    _willUpdatePreferencesSoon = NO;

    if (!_pdfViewController)
        return;

    WebPreferences* preferences = toImpl([_pdfViewController->wkView() pageRef])->pageGroup()->preferences();

    CGFloat scaleFactor = [_pdfView autoScales] ? 0 : [_pdfView scaleFactor];
    preferences->setPDFScaleFactor(scaleFactor);
    preferences->setPDFDisplayMode([_pdfView displayMode]);
}

- (void)_updatePreferencesSoon
{   
    if (_willUpdatePreferencesSoon)
        return;

    [self performSelector:@selector(_updatePreferences:) withObject:nil afterDelay:0];
    _willUpdatePreferencesSoon = YES;
}

- (void)_scaleOrDisplayModeOrPageChanged:(NSNotification *)notification
{
    ASSERT_ARG(notification, [notification object] == _pdfView);
    if (!_ignoreScaleAndDisplayModeAndPageNotifications)
        [self _updatePreferencesSoon];
}

- (void)viewDidMoveToWindow
{
    if (![self window])
        return;

    NSNotificationCenter *notificationCenter = [NSNotificationCenter defaultCenter];
    [notificationCenter addObserver:self selector:@selector(_scaleOrDisplayModeOrPageChanged:) name:_webkit_PDFViewScaleChangedNotification object:_pdfView];
    [notificationCenter addObserver:self selector:@selector(_scaleOrDisplayModeOrPageChanged:) name:_webkit_PDFViewDisplayModeChangedNotification object:_pdfView];
    [notificationCenter addObserver:self selector:@selector(_scaleOrDisplayModeOrPageChanged:) name:_webkit_PDFViewPageChangedNotification object:_pdfView];
}

- (void)viewWillMoveToWindow:(NSWindow *)newWindow
{
    if (![self window])
        return;

    NSNotificationCenter *notificationCenter = [NSNotificationCenter defaultCenter];
    [notificationCenter removeObserver:self name:_webkit_PDFViewScaleChangedNotification object:_pdfView];
    [notificationCenter removeObserver:self name:_webkit_PDFViewDisplayModeChangedNotification object:_pdfView];
    [notificationCenter removeObserver:self name:_webkit_PDFViewPageChangedNotification object:_pdfView];
}

@end

namespace WebKit {

PassOwnPtr<PDFViewController> PDFViewController::create(WKView *wkView)
{
    return adoptPtr(new PDFViewController(wkView));
}

PDFViewController::PDFViewController(WKView *wkView)
    : m_wkView(wkView)
    , m_wkPDFView(AdoptNS, [[WKPDFView alloc] initWithFrame:[m_wkView bounds] PDFViewController:this])
    , m_pdfView([m_wkPDFView.get() pdfView])
{
    [m_wkView addSubview:m_wkPDFView.get()];
}

PDFViewController::~PDFViewController()
{
    [m_wkPDFView.get() removeFromSuperview];
    [m_wkPDFView.get() invalidate];
    m_wkPDFView = nullptr;
}

static RetainPtr<CFDataRef> convertPostScriptDataSourceToPDF(const CoreIPC::DataReference& dataReference)
{
    // Convert PostScript to PDF using Quartz 2D API
    // http://developer.apple.com/documentation/GraphicsImaging/Conceptual/drawingwithquartz2d/dq_ps_convert/chapter_16_section_1.html
    
    CGPSConverterCallbacks callbacks = { 0, 0, 0, 0, 0, 0, 0, 0 };    
    RetainPtr<CGPSConverterRef> converter(AdoptCF, CGPSConverterCreate(0, &callbacks, 0));
    ASSERT(converter);

    RetainPtr<NSData> nsData(AdoptNS, [[NSData alloc] initWithBytesNoCopy:const_cast<uint8_t*>(dataReference.data()) length:dataReference.size() freeWhenDone:NO]);   

    RetainPtr<CGDataProviderRef> provider(AdoptCF, CGDataProviderCreateWithCFData((CFDataRef)nsData.get()));
    ASSERT(provider);

    RetainPtr<CFMutableDataRef> result(AdoptCF, CFDataCreateMutable(kCFAllocatorDefault, 0));
    ASSERT(result);
    
    RetainPtr<CGDataConsumerRef> consumer(AdoptCF, CGDataConsumerCreateWithCFData(result.get()));
    ASSERT(consumer);
    
    CGPSConverterConvert(converter.get(), provider.get(), consumer.get(), 0);

    if (!result)
        return 0;

    return result;
}

void PDFViewController::setPDFDocumentData(const String& mimeType, const CoreIPC::DataReference& dataReference)
{
    RetainPtr<CFDataRef> data;
    
    if (equalIgnoringCase(mimeType, "application/postscript")) {
        data = convertPostScriptDataSourceToPDF(dataReference);
        if (!data)
            return;
    } else {
        // Make sure to copy the data.
        data.adoptCF(CFDataCreate(0, dataReference.data(), dataReference.size()));
    }

    RetainPtr<PDFDocument> pdfDocument(AdoptNS, [[pdfDocumentClass() alloc] initWithData:(NSData *)data.get()]);
    [m_wkPDFView.get() setDocument:pdfDocument.get()];
}

double PDFViewController::zoomFactor() const
{
    return [m_pdfView scaleFactor];
}

void PDFViewController::setZoomFactor(double zoomFactor)
{
    [m_pdfView setScaleFactor:zoomFactor];
}

Class PDFViewController::pdfDocumentClass()
{
    static Class pdfDocumentClass = [pdfKitBundle() classNamed:@"PDFDocument"];
    
    return pdfDocumentClass;
}

Class PDFViewController::pdfPreviewViewClass()
{
    static Class pdfPreviewViewClass = [pdfKitBundle() classNamed:@"PDFPreviewView"];
    
    return pdfPreviewViewClass;
}
    
NSBundle* PDFViewController::pdfKitBundle()
{
    static NSBundle *pdfKitBundle;
    if (pdfKitBundle)
        return pdfKitBundle;

    NSString *pdfKitPath = [_NSPathForSystemFramework(@"Quartz.framework") stringByAppendingString:@"/Frameworks/PDFKit.framework"];
    if (!pdfKitPath) {
        LOG_ERROR("Couldn't find PDFKit.framework");
        return nil;
    }

    pdfKitBundle = [NSBundle bundleWithPath:pdfKitPath];
    if (![pdfKitBundle load])
        LOG_ERROR("Couldn't load PDFKit.framework");
    return pdfKitBundle;
}

NSPrintOperation *PDFViewController::makePrintOperation(NSPrintInfo *printInfo)
{
    return [[m_pdfView document] getPrintOperationForPrintInfo:printInfo autoRotate:YES];
}

} // namespace WebKit