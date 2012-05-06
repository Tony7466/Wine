/*
 * Copyright 2005-2009 Jacek Caban for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "wingdi.h"
#include "docobj.h"
#include "comcat.h"
#include "mshtml.h"
#include "mshtmhst.h"
#include "hlink.h"
#include "perhist.h"
#include "dispex.h"
#include "objsafe.h"

#include "wine/list.h"
#include "wine/unicode.h"

#ifdef INIT_GUID
#include "initguid.h"
#endif

#include "nsiface.h"

#define NS_ERROR_GENERATE_FAILURE(module,code) \
    ((nsresult) (((PRUint32)(1<<31)) | ((PRUint32)(module+0x45)<<16) | ((PRUint32)(code))))

#define NS_OK                     ((nsresult)0x00000000L)
#define NS_ERROR_FAILURE          ((nsresult)0x80004005L)
#define NS_ERROR_OUT_OF_MEMORY    ((nsresult)0x8007000EL)
#define NS_NOINTERFACE            ((nsresult)0x80004002L)
#define NS_ERROR_NOT_IMPLEMENTED  ((nsresult)0x80004001L)
#define NS_ERROR_NOT_AVAILABLE    ((nsresult)0x80040111L)
#define NS_ERROR_INVALID_ARG      ((nsresult)0x80070057L) 
#define NS_ERROR_UNEXPECTED       ((nsresult)0x8000ffffL)

#define NS_ERROR_MODULE_NETWORK    6

#define NS_BINDING_ABORTED         NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_NETWORK, 2)
#define NS_ERROR_UNKNOWN_PROTOCOL  NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_NETWORK, 18)

#define NS_FAILED(res) ((res) & 0x80000000)
#define NS_SUCCEEDED(res) (!NS_FAILED(res))

#define NSAPI WINAPI

#define MSHTML_E_NODOC    0x800a025c

typedef struct HTMLDOMNode HTMLDOMNode;
typedef struct ConnectionPoint ConnectionPoint;
typedef struct BSCallback BSCallback;
typedef struct event_target_t event_target_t;

#define TID_LIST \
    XIID(NULL) \
    XDIID(DispCEventObj) \
    XDIID(DispCPlugins) \
    XDIID(DispDOMChildrenCollection) \
    XDIID(DispHTMLAnchorElement) \
    XDIID(DispHTMLBody) \
    XDIID(DispHTMLCommentElement) \
    XDIID(DispHTMLCurrentStyle) \
    XDIID(DispHTMLDocument) \
    XDIID(DispHTMLDOMAttribute) \
    XDIID(DispHTMLDOMTextNode) \
    XDIID(DispHTMLElementCollection) \
    XDIID(DispHTMLEmbed) \
    XDIID(DispHTMLFormElement) \
    XDIID(DispHTMLGenericElement) \
    XDIID(DispHTMLFrameElement) \
    XDIID(DispHTMLIFrame) \
    XDIID(DispHTMLImg) \
    XDIID(DispHTMLInputElement) \
    XDIID(DispHTMLLocation) \
    XDIID(DispHTMLNavigator) \
    XDIID(DispHTMLObjectElement) \
    XDIID(DispHTMLOptionElement) \
    XDIID(DispHTMLScreen) \
    XDIID(DispHTMLScriptElement) \
    XDIID(DispHTMLSelectElement) \
    XDIID(DispHTMLStyle) \
    XDIID(DispHTMLStyleElement) \
    XDIID(DispHTMLStyleSheetsCollection) \
    XDIID(DispHTMLTable) \
    XDIID(DispHTMLTableRow) \
    XDIID(DispHTMLTextAreaElement) \
    XDIID(DispHTMLUnknownElement) \
    XDIID(DispHTMLWindow2) \
    XDIID(HTMLDocumentEvents) \
    XIID(IHTMLAnchorElement) \
    XIID(IHTMLBodyElement) \
    XIID(IHTMLBodyElement2) \
    XIID(IHTMLCommentElement) \
    XIID(IHTMLCurrentStyle) \
    XIID(IHTMLCurrentStyle2) \
    XIID(IHTMLCurrentStyle3) \
    XIID(IHTMLCurrentStyle4) \
    XIID(IHTMLDocument2) \
    XIID(IHTMLDocument3) \
    XIID(IHTMLDocument4) \
    XIID(IHTMLDocument5) \
    XIID(IHTMLDOMAttribute) \
    XIID(IHTMLDOMChildrenCollection) \
    XIID(IHTMLDOMNode) \
    XIID(IHTMLDOMNode2) \
    XIID(IHTMLDOMTextNode) \
    XIID(IHTMLElement) \
    XIID(IHTMLElement2) \
    XIID(IHTMLElement3) \
    XIID(IHTMLElement4) \
    XIID(IHTMLElementCollection) \
    XIID(IHTMLEmbedElement) \
    XIID(IHTMLEventObj) \
    XIID(IHTMLFiltersCollection) \
    XIID(IHTMLFormElement) \
    XIID(IHTMLFrameBase) \
    XIID(IHTMLFrameBase2) \
    XIID(IHTMLFrameElement3) \
    XIID(IHTMLGenericElement) \
    XIID(IHTMLIFrameElement) \
    XIID(IHTMLImageElementFactory) \
    XIID(IHTMLImgElement) \
    XIID(IHTMLInputElement) \
    XIID(IHTMLLocation) \
    XIID(IHTMLObjectElement) \
    XIID(IHTMLOptionElement) \
    XIID(IHTMLPluginsCollection) \
    XIID(IHTMLRect) \
    XIID(IHTMLScreen) \
    XIID(IHTMLScriptElement) \
    XIID(IHTMLSelectElement) \
    XIID(IHTMLStyle) \
    XIID(IHTMLStyle2) \
    XIID(IHTMLStyle3) \
    XIID(IHTMLStyle4) \
    XIID(IHTMLStyleElement) \
    XIID(IHTMLStyleSheetsCollection) \
    XIID(IHTMLTable) \
    XIID(IHTMLTableRow) \
    XIID(IHTMLTextAreaElement) \
    XIID(IHTMLTextContainer) \
    XIID(IHTMLUniqueName) \
    XIID(IHTMLWindow2) \
    XIID(IHTMLWindow3) \
    XIID(IHTMLWindow4) \
    XIID(IOmNavigator)

typedef enum {
#define XIID(iface) iface ## _tid,
#define XDIID(iface) iface ## _tid,
TID_LIST
#undef XIID
#undef XDIID
    LAST_tid
} tid_t;

typedef struct dispex_data_t dispex_data_t;
typedef struct dispex_dynamic_data_t dispex_dynamic_data_t;

#define MSHTML_DISPID_CUSTOM_MIN 0x60000000
#define MSHTML_DISPID_CUSTOM_MAX 0x6fffffff
#define MSHTML_CUSTOM_DISPID_CNT (MSHTML_DISPID_CUSTOM_MAX-MSHTML_DISPID_CUSTOM_MIN)

typedef struct DispatchEx DispatchEx;

typedef struct {
    HRESULT (*value)(DispatchEx*,LCID,WORD,DISPPARAMS*,VARIANT*,EXCEPINFO*,IServiceProvider*);
    HRESULT (*get_dispid)(DispatchEx*,BSTR,DWORD,DISPID*);
    HRESULT (*invoke)(DispatchEx*,DISPID,LCID,WORD,DISPPARAMS*,VARIANT*,EXCEPINFO*,IServiceProvider*);
} dispex_static_data_vtbl_t;

typedef struct {
    const dispex_static_data_vtbl_t *vtbl;
    const tid_t disp_tid;
    dispex_data_t *data;
    const tid_t* const iface_tids;
} dispex_static_data_t;

struct DispatchEx {
    IDispatchEx IDispatchEx_iface;

    IUnknown *outer;

    dispex_static_data_t *data;
    dispex_dynamic_data_t *dynamic_data;
};

void init_dispex(DispatchEx*,IUnknown*,dispex_static_data_t*);
void release_dispex(DispatchEx*);
BOOL dispex_query_interface(DispatchEx*,REFIID,void**);
HRESULT dispex_get_dprop_ref(DispatchEx*,const WCHAR*,BOOL,VARIANT**);
HRESULT get_dispids(tid_t,DWORD*,DISPID**);
HRESULT remove_prop(DispatchEx*,BSTR,VARIANT_BOOL*);
void release_typelib(void);

typedef struct HTMLWindow HTMLWindow;
typedef struct HTMLDocumentNode HTMLDocumentNode;
typedef struct HTMLDocumentObj HTMLDocumentObj;
typedef struct HTMLFrameBase HTMLFrameBase;
typedef struct NSContainer NSContainer;

typedef enum {
    SCRIPTMODE_GECKO,
    SCRIPTMODE_ACTIVESCRIPT
} SCRIPTMODE;

typedef struct ScriptHost ScriptHost;

typedef enum {
    GLOBAL_SCRIPTVAR,
    GLOBAL_ELEMENTVAR
} global_prop_type_t;

typedef struct {
    global_prop_type_t type;
    WCHAR *name;
    ScriptHost *script_host;
    DISPID id;
} global_prop_t;

typedef struct {
    IHTMLOptionElementFactory IHTMLOptionElementFactory_iface;

    LONG ref;

    HTMLWindow *window;
} HTMLOptionElementFactory;

typedef struct {
    DispatchEx dispex;
    IHTMLImageElementFactory IHTMLImageElementFactory_iface;

    LONG ref;

    HTMLWindow *window;
} HTMLImageElementFactory;

struct HTMLLocation {
    DispatchEx dispex;
    IHTMLLocation IHTMLLocation_iface;

    LONG ref;

    HTMLWindow *window;
};

typedef struct {
    HTMLWindow *window;
    LONG ref;
}  windowref_t;

typedef struct nsChannelBSC nsChannelBSC;

struct HTMLWindow {
    DispatchEx dispex;
    IHTMLWindow2       IHTMLWindow2_iface;
    IHTMLWindow3       IHTMLWindow3_iface;
    IHTMLWindow4       IHTMLWindow4_iface;
    IHTMLPrivateWindow IHTMLPrivateWindow_iface;
    IDispatchEx        IDispatchEx_iface;
    IServiceProvider   IServiceProvider_iface;

    LONG ref;

    windowref_t *window_ref;
    LONG task_magic;

    HTMLDocumentNode *doc;
    HTMLDocumentObj *doc_obj;
    nsIDOMWindow *nswindow;
    HTMLWindow *parent;
    HTMLFrameBase *frame_element;
    READYSTATE readystate;

    nsChannelBSC *bscallback;
    IMoniker *mon;
    LPOLESTR url;

    IHTMLEventObj *event;

    SCRIPTMODE scriptmode;
    struct list script_hosts;

    HTMLOptionElementFactory *option_factory;
    HTMLImageElementFactory *image_factory;
    HTMLLocation *location;
    IHTMLScreen *screen;

    global_prop_t *global_props;
    DWORD global_prop_cnt;
    DWORD global_prop_size;

    struct list children;
    struct list sibling_entry;
    struct list entry;
};

typedef enum {
    UNKNOWN_USERMODE,
    BROWSEMODE,
    EDITMODE        
} USERMODE;

typedef struct _cp_static_data_t {
    tid_t tid;
    void (*on_advise)(IUnknown*,struct _cp_static_data_t*);
    DWORD id_cnt;
    DISPID *ids;
} cp_static_data_t;

typedef struct ConnectionPointContainer {
    IConnectionPointContainer IConnectionPointContainer_iface;

    ConnectionPoint *cp_list;
    IUnknown *outer;
    struct ConnectionPointContainer *forward_container;
} ConnectionPointContainer;

struct ConnectionPoint {
    IConnectionPoint IConnectionPoint_iface;

    ConnectionPointContainer *container;

    union {
        IUnknown *unk;
        IDispatch *disp;
        IPropertyNotifySink *propnotif;
    } *sinks;
    DWORD sinks_size;

    const IID *iid;
    cp_static_data_t *data;

    ConnectionPoint *next;
};

struct HTMLDocument {
    IHTMLDocument2              IHTMLDocument2_iface;
    IHTMLDocument3              IHTMLDocument3_iface;
    IHTMLDocument4              IHTMLDocument4_iface;
    IHTMLDocument5              IHTMLDocument5_iface;
    IHTMLDocument6              IHTMLDocument6_iface;
    IPersistMoniker             IPersistMoniker_iface;
    IPersistFile                IPersistFile_iface;
    IPersistHistory             IPersistHistory_iface;
    IMonikerProp                IMonikerProp_iface;
    IOleObject                  IOleObject_iface;
    IOleDocument                IOleDocument_iface;
    IOleDocumentView            IOleDocumentView_iface;
    IOleInPlaceActiveObject     IOleInPlaceActiveObject_iface;
    IViewObjectEx               IViewObjectEx_iface;
    IOleInPlaceObjectWindowless IOleInPlaceObjectWindowless_iface;
    IServiceProvider            IServiceProvider_iface;
    IOleCommandTarget           IOleCommandTarget_iface;
    IOleControl                 IOleControl_iface;
    IHlinkTarget                IHlinkTarget_iface;
    IPersistStreamInit          IPersistStreamInit_iface;
    IDispatchEx                 IDispatchEx_iface;
    ISupportErrorInfo           ISupportErrorInfo_iface;
    IObjectWithSite             IObjectWithSite_iface;
    IOleContainer               IOleContainer_iface;
    IObjectSafety               IObjectSafety_iface;

    IUnknown *unk_impl;
    IDispatchEx *dispex;

    HTMLDocumentObj *doc_obj;
    HTMLDocumentNode *doc_node;

    HTMLWindow *window;

    LONG task_magic;

    ConnectionPointContainer cp_container;
    ConnectionPoint cp_htmldocevents;
    ConnectionPoint cp_htmldocevents2;
    ConnectionPoint cp_propnotif;
    ConnectionPoint cp_dispatch;

    IOleAdviseHolder *advise_holder;
};

static inline HRESULT htmldoc_query_interface(HTMLDocument *This, REFIID riid, void **ppv)
{
    return IUnknown_QueryInterface(This->unk_impl, riid, ppv);
}

static inline ULONG htmldoc_addref(HTMLDocument *This)
{
    return IUnknown_AddRef(This->unk_impl);
}

static inline ULONG htmldoc_release(HTMLDocument *This)
{
    return IUnknown_Release(This->unk_impl);
}

struct HTMLDocumentObj {
    HTMLDocument basedoc;
    DispatchEx dispex;
    ICustomDoc ICustomDoc_iface;

    LONG ref;

    NSContainer *nscontainer;

    IOleClientSite *client;
    IDocHostUIHandler *hostui;
    BOOL custom_hostui;
    IOleInPlaceSite *ipsite;
    IOleInPlaceFrame *frame;
    IOleInPlaceUIWindow *ip_window;
    IAdviseSink *view_sink;

    DOCHOSTUIINFO hostinfo;

    IOleUndoManager *undomgr;

    HWND hwnd;
    HWND tooltips_hwnd;

    BOOL request_uiactivate;
    BOOL in_place_active;
    BOOL ui_active;
    BOOL window_active;
    BOOL hostui_setup;
    BOOL container_locked;
    BOOL focus;
    INT download_state;

    USERMODE usermode;
    LPWSTR mime;

    DWORD update;
};

struct NSContainer {
    nsIWebBrowserChrome      nsIWebBrowserChrome_iface;
    nsIContextMenuListener   nsIContextMenuListener_iface;
    nsIURIContentListener    nsIURIContentListener_iface;
    nsIEmbeddingSiteWindow   nsIEmbeddingSiteWindow_iface;
    nsITooltipListener       nsITooltipListener_iface;
    nsIInterfaceRequestor    nsIInterfaceRequestor_iface;
    nsIWeakReference         nsIWeakReference_iface;
    nsISupportsWeakReference nsISupportsWeakReference_iface;

    nsIWebBrowser *webbrowser;
    nsIWebNavigation *navigation;
    nsIBaseWindow *window;
    nsIWebBrowserFocus *focus;

    nsIEditor *editor;
    nsIController *editor_controller;

    LONG ref;

    NSContainer *parent;
    HTMLDocumentObj *doc;

    nsIURIContentListener *content_listener;

    HWND hwnd;
};

typedef struct nsWineURI nsWineURI;

HRESULT set_wine_url(nsWineURI*,LPCWSTR);
nsresult on_start_uri_open(NSContainer*,nsIURI*,PRBool*);

/* Keep sync with request_method_strings in nsio.c */
typedef enum {
    METHOD_GET,
    METHOD_PUT,
    METHOD_POST
} REQUEST_METHOD;

typedef struct {
    nsIHttpChannel         nsIHttpChannel_iface;
    nsIUploadChannel       nsIUploadChannel_iface;
    nsIHttpChannelInternal nsIHttpChannelInternal_iface;

    LONG ref;

    nsWineURI *uri;
    nsIInputStream *post_data_stream;
    nsILoadGroup *load_group;
    nsIInterfaceRequestor *notif_callback;
    nsISupports *owner;
    nsLoadFlags load_flags;
    nsIURI *original_uri;
    nsIURI *referrer;
    char *content_type;
    char *charset;
    PRUint32 response_status;
    REQUEST_METHOD request_method;
    struct list response_headers;
    struct list request_headers;
    UINT url_scheme;
} nsChannel;

typedef struct {
    struct list entry;
    WCHAR *header;
    WCHAR *data;
} http_header_t;

HRESULT set_http_header(struct list*,const WCHAR*,int,const WCHAR*,int);

typedef struct {
    HRESULT (*qi)(HTMLDOMNode*,REFIID,void**);
    void (*destructor)(HTMLDOMNode*);
    HRESULT (*clone)(HTMLDOMNode*,nsIDOMNode*,HTMLDOMNode**);
    event_target_t **(*get_event_target)(HTMLDOMNode*);
    HRESULT (*call_event)(HTMLDOMNode*,DWORD,BOOL*);
    HRESULT (*put_disabled)(HTMLDOMNode*,VARIANT_BOOL);
    HRESULT (*get_disabled)(HTMLDOMNode*,VARIANT_BOOL*);
    HRESULT (*get_document)(HTMLDOMNode*,IDispatch**);
    HRESULT (*get_readystate)(HTMLDOMNode*,BSTR*);
    HRESULT (*get_dispid)(HTMLDOMNode*,BSTR,DWORD,DISPID*);
    HRESULT (*invoke)(HTMLDOMNode*,DISPID,LCID,WORD,DISPPARAMS*,VARIANT*,EXCEPINFO*,IServiceProvider*);
    HRESULT (*bind_to_tree)(HTMLDOMNode*);
} NodeImplVtbl;

struct HTMLDOMNode {
    DispatchEx dispex;
    IHTMLDOMNode  IHTMLDOMNode_iface;
    IHTMLDOMNode2 IHTMLDOMNode2_iface;
    const NodeImplVtbl *vtbl;

    LONG ref;

    nsIDOMNode *nsnode;
    HTMLDocumentNode *doc;
    event_target_t *event_target;
    ConnectionPointContainer *cp_container;

    HTMLDOMNode *next;
};

typedef struct {
    HTMLDOMNode node;
    ConnectionPointContainer cp_container;

    IHTMLElement  IHTMLElement_iface;
    IHTMLElement2 IHTMLElement2_iface;
    IHTMLElement3 IHTMLElement3_iface;
    IHTMLElement4 IHTMLElement4_iface;

    nsIDOMHTMLElement *nselem;
    HTMLStyle *style;
    struct list attrs;
} HTMLElement;

#define HTMLELEMENT_TIDS    \
    IHTMLDOMNode_tid,       \
    IHTMLDOMNode2_tid,      \
    IHTMLElement_tid,       \
    IHTMLElement2_tid,      \
    IHTMLElement3_tid,      \
    IHTMLElement4_tid

typedef struct {
    HTMLElement element;

    IHTMLTextContainer IHTMLTextContainer_iface;

    ConnectionPoint cp;
} HTMLTextContainer;

struct HTMLFrameBase {
    HTMLElement element;

    IHTMLFrameBase  IHTMLFrameBase_iface;
    IHTMLFrameBase2 IHTMLFrameBase2_iface;

    HTMLWindow *content_window;

    nsIDOMHTMLFrameElement *nsframe;
    nsIDOMHTMLIFrameElement *nsiframe;
};

typedef struct nsDocumentEventListener nsDocumentEventListener;

struct HTMLDocumentNode {
    HTMLDOMNode node;
    HTMLDocument basedoc;

    IInternetHostSecurityManager IInternetHostSecurityManager_iface;

    nsIDocumentObserver          nsIDocumentObserver_iface;

    LONG ref;

    nsIDOMHTMLDocument *nsdoc;
    HTMLDOMNode *nodes;
    BOOL content_ready;
    event_target_t *body_event_target;

    IInternetSecurityManager *secmgr;
    ICatInformation *catmgr;
    nsDocumentEventListener *nsevent_listener;
    BOOL *event_vector;

    BOOL skip_mutation_notif;

    struct list bindings;
    struct list selection_list;
    struct list range_list;
    struct list plugin_hosts;
};

HRESULT HTMLDocument_Create(IUnknown*,REFIID,void**);
HRESULT HTMLLoadOptions_Create(IUnknown*,REFIID,void**);
HRESULT create_doc_from_nsdoc(nsIDOMHTMLDocument*,HTMLDocumentObj*,HTMLWindow*,HTMLDocumentNode**);
HRESULT create_document_fragment(nsIDOMNode*,HTMLDocumentNode*,HTMLDocumentNode**);

HRESULT HTMLWindow_Create(HTMLDocumentObj*,nsIDOMWindow*,HTMLWindow*,HTMLWindow**);
void update_window_doc(HTMLWindow*);
HTMLWindow *nswindow_to_window(const nsIDOMWindow*);
nsIDOMWindow *get_nsdoc_window(nsIDOMDocument*);
HTMLOptionElementFactory *HTMLOptionElementFactory_Create(HTMLWindow*);
HTMLImageElementFactory *HTMLImageElementFactory_Create(HTMLWindow*);
HRESULT HTMLLocation_Create(HTMLWindow*,HTMLLocation**);
IOmNavigator *OmNavigator_Create(void);
HRESULT HTMLScreen_Create(IHTMLScreen**);

void HTMLDocument_HTMLDocument3_Init(HTMLDocument*);
void HTMLDocument_HTMLDocument5_Init(HTMLDocument*);
void HTMLDocument_Persist_Init(HTMLDocument*);
void HTMLDocument_OleCmd_Init(HTMLDocument*);
void HTMLDocument_OleObj_Init(HTMLDocument*);
void HTMLDocument_View_Init(HTMLDocument*);
void HTMLDocument_Window_Init(HTMLDocument*);
void HTMLDocument_Service_Init(HTMLDocument*);
void HTMLDocument_Hlink_Init(HTMLDocument*);

void HTMLDocumentNode_SecMgr_Init(HTMLDocumentNode*);

HRESULT HTMLCurrentStyle_Create(HTMLElement*,IHTMLCurrentStyle**);

void ConnectionPoint_Init(ConnectionPoint*,ConnectionPointContainer*,REFIID,cp_static_data_t*);
void ConnectionPointContainer_Init(ConnectionPointContainer*,IUnknown*);
void ConnectionPointContainer_Destroy(ConnectionPointContainer*);

NSContainer *NSContainer_Create(HTMLDocumentObj*,NSContainer*);
void NSContainer_Release(NSContainer*);
nsresult create_chrome_window(nsIWebBrowserChrome*,nsIWebBrowserChrome**);

void init_mutation(HTMLDocumentNode*);
void release_mutation(HTMLDocumentNode*);

void HTMLDocument_LockContainer(HTMLDocumentObj*,BOOL);
void show_context_menu(HTMLDocumentObj*,DWORD,POINT*,IDispatch*);
void notif_focus(HTMLDocumentObj*);

void show_tooltip(HTMLDocumentObj*,DWORD,DWORD,LPCWSTR);
void hide_tooltip(HTMLDocumentObj*);
HRESULT get_client_disp_property(IOleClientSite*,DISPID,VARIANT*);

HRESULT ProtocolFactory_Create(REFCLSID,REFIID,void**);

BOOL load_gecko(BOOL);
void close_gecko(void);
void register_nsservice(nsIComponentRegistrar*,nsIServiceManager*);
void init_nsio(nsIComponentManager*,nsIComponentRegistrar*);
void release_nsio(void);
BOOL is_gecko_path(const char*);

HRESULT nsuri_to_url(LPCWSTR,BOOL,BSTR*);
HRESULT create_doc_uri(HTMLWindow*,WCHAR*,nsWineURI**);
HRESULT load_nsuri(HTMLWindow*,nsWineURI*,nsChannelBSC*,DWORD);

HRESULT hlink_frame_navigate(HTMLDocument*,LPCWSTR,nsIInputStream*,DWORD,BOOL*);
HRESULT navigate_url(HTMLWindow*,const WCHAR*,const WCHAR*);
HRESULT set_frame_doc(HTMLFrameBase*,nsIDOMDocument*);
HRESULT set_moniker(HTMLDocument*,IMoniker*,IBindCtx*,nsChannelBSC*,BOOL);

void call_property_onchanged(ConnectionPoint*,DISPID);
HRESULT call_set_active_object(IOleInPlaceUIWindow*,IOleInPlaceActiveObject*);

void *nsalloc(size_t) __WINE_ALLOC_SIZE(1);
void nsfree(void*);

void nsACString_InitDepend(nsACString*,const char*);
void nsACString_SetData(nsACString*,const char*);
PRUint32 nsACString_GetData(const nsACString*,const char**);
void nsACString_Finish(nsACString*);

BOOL nsAString_Init(nsAString*,const PRUnichar*);
void nsAString_InitDepend(nsAString*,const PRUnichar*);
void nsAString_SetData(nsAString*,const PRUnichar*);
PRUint32 nsAString_GetData(const nsAString*,const PRUnichar**);
void nsAString_Finish(nsAString*);
HRESULT return_nsstr(nsresult,nsAString*,BSTR*);

nsICommandParams *create_nscommand_params(void);
HRESULT nsnode_to_nsstring(nsIDOMNode*,nsAString*);
void get_editor_controller(NSContainer*);
nsresult get_nsinterface(nsISupports*,REFIID,void**);

void init_nsevents(HTMLDocumentNode*);
void release_nsevents(HTMLDocumentNode*);
void add_nsevent_listener(HTMLDocumentNode*,nsIDOMNode*,LPCWSTR);

void set_window_bscallback(HTMLWindow*,nsChannelBSC*);
void set_current_mon(HTMLWindow*,IMoniker*);
HRESULT start_binding(HTMLWindow*,HTMLDocumentNode*,BSCallback*,IBindCtx*);
HRESULT async_start_doc_binding(HTMLWindow*,nsChannelBSC*);
void abort_document_bindings(HTMLDocumentNode*);

HRESULT bind_mon_to_buffer(HTMLDocumentNode*,IMoniker*,void**,DWORD*);

HRESULT create_channelbsc(IMoniker*,WCHAR*,BYTE*,DWORD,nsChannelBSC**);
HRESULT channelbsc_load_stream(nsChannelBSC*,IStream*);
void channelbsc_set_channel(nsChannelBSC*,nsChannel*,nsIStreamListener*,nsISupports*);
IMoniker *get_channelbsc_mon(nsChannelBSC*);

void set_ready_state(HTMLWindow*,READYSTATE);

HRESULT HTMLSelectionObject_Create(HTMLDocumentNode*,nsISelection*,IHTMLSelectionObject**);
HRESULT HTMLTxtRange_Create(HTMLDocumentNode*,nsIDOMRange*,IHTMLTxtRange**);
HRESULT HTMLStyle_Create(nsIDOMCSSStyleDeclaration*,HTMLStyle**);
IHTMLStyleSheet *HTMLStyleSheet_Create(nsIDOMStyleSheet*);
IHTMLStyleSheetsCollection *HTMLStyleSheetsCollection_Create(nsIDOMStyleSheetList*);

void detach_selection(HTMLDocumentNode*);
void detach_ranges(HTMLDocumentNode*);
HRESULT get_node_text(HTMLDOMNode*,BSTR*);

HRESULT create_nselem(HTMLDocumentNode*,const WCHAR*,nsIDOMHTMLElement**);

HRESULT HTMLDOMTextNode_Create(HTMLDocumentNode*,nsIDOMNode*,HTMLDOMNode**);

typedef struct {
    DispatchEx dispex;
    IHTMLDOMAttribute IHTMLDOMAttribute_iface;

    LONG ref;

    DISPID dispid;
    HTMLElement *elem;
    struct list entry;
} HTMLDOMAttribute;

HRESULT HTMLDOMAttribute_Create(HTMLElement*,DISPID,HTMLDOMAttribute**);

HRESULT HTMLElement_Create(HTMLDocumentNode*,nsIDOMNode*,BOOL,HTMLElement**);
HRESULT HTMLCommentElement_Create(HTMLDocumentNode*,nsIDOMNode*,HTMLElement**);
HRESULT HTMLAnchorElement_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLBodyElement_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLEmbedElement_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLFormElement_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLFrameElement_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLIFrame_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLStyleElement_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLImgElement_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLInputElement_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLObjectElement_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLOptionElement_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLScriptElement_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLSelectElement_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLTable_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLTableRow_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLTextAreaElement_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);
HRESULT HTMLGenericElement_Create(HTMLDocumentNode*,nsIDOMHTMLElement*,HTMLElement**);

void HTMLDOMNode_Init(HTMLDocumentNode*,HTMLDOMNode*,nsIDOMNode*);
void HTMLElement_Init(HTMLElement*,HTMLDocumentNode*,nsIDOMHTMLElement*,dispex_static_data_t*);
void HTMLElement2_Init(HTMLElement*);
void HTMLElement3_Init(HTMLElement*);
void HTMLTextContainer_Init(HTMLTextContainer*,HTMLDocumentNode*,nsIDOMHTMLElement*,dispex_static_data_t*);
void HTMLFrameBase_Init(HTMLFrameBase*,HTMLDocumentNode*,nsIDOMHTMLElement*,dispex_static_data_t*);

HRESULT HTMLDOMNode_QI(HTMLDOMNode*,REFIID,void**);
void HTMLDOMNode_destructor(HTMLDOMNode*);

HRESULT HTMLElement_QI(HTMLDOMNode*,REFIID,void**);
void HTMLElement_destructor(HTMLDOMNode*);
HRESULT HTMLElement_clone(HTMLDOMNode*,nsIDOMNode*,HTMLDOMNode**);

HRESULT HTMLFrameBase_QI(HTMLFrameBase*,REFIID,void**);
void HTMLFrameBase_destructor(HTMLFrameBase*);

HRESULT get_node(HTMLDocumentNode*,nsIDOMNode*,BOOL,HTMLDOMNode**);
void release_nodes(HTMLDocumentNode*);

void release_script_hosts(HTMLWindow*);
void connect_scripts(HTMLWindow*);
void doc_insert_script(HTMLWindow*,nsIDOMHTMLScriptElement*);
IDispatch *script_parse_event(HTMLWindow*,LPCWSTR);
HRESULT exec_script(HTMLWindow*,const WCHAR*,const WCHAR*,VARIANT*);
void set_script_mode(HTMLWindow*,SCRIPTMODE);
BOOL find_global_prop(HTMLWindow*,BSTR,DWORD,ScriptHost**,DISPID*);
IDispatch *get_script_disp(ScriptHost*);
HRESULT search_window_props(HTMLWindow*,BSTR,DWORD,DISPID*);

IHTMLElementCollection *create_all_collection(HTMLDOMNode*,BOOL);
IHTMLElementCollection *create_collection_from_nodelist(HTMLDocumentNode*,IUnknown*,nsIDOMNodeList*);
IHTMLElementCollection *create_collection_from_htmlcol(HTMLDocumentNode*,IUnknown*,nsIDOMHTMLCollection*);

/* commands */
typedef struct {
    DWORD id;
    HRESULT (*query)(HTMLDocument*,OLECMD*);
    HRESULT (*exec)(HTMLDocument*,DWORD,VARIANT*,VARIANT*);
} cmdtable_t;

extern const cmdtable_t editmode_cmds[];

void do_ns_command(HTMLDocument*,const char*,nsICommandParams*);

/* timer */
#define UPDATE_UI       0x0001
#define UPDATE_TITLE    0x0002

void update_doc(HTMLDocument*,DWORD);
void update_title(HTMLDocumentObj*);

/* editor */
void init_editor(HTMLDocument*);
void handle_edit_event(HTMLDocument*,nsIDOMEvent*);
HRESULT editor_exec_copy(HTMLDocument*,DWORD,VARIANT*,VARIANT*);
HRESULT editor_exec_cut(HTMLDocument*,DWORD,VARIANT*,VARIANT*);
HRESULT editor_exec_paste(HTMLDocument*,DWORD,VARIANT*,VARIANT*);
void handle_edit_load(HTMLDocument*);
HRESULT editor_is_dirty(HTMLDocument*);
void set_dirty(HTMLDocument*,VARIANT_BOOL);

extern DWORD mshtml_tls;

typedef struct task_t task_t;
typedef void (*task_proc_t)(task_t*);

struct task_t {
    LONG target_magic;
    task_proc_t proc;
    struct task_t *next;
};

typedef struct {
    task_t header;
    HTMLDocumentObj *doc;
} docobj_task_t;

typedef struct {
    HWND thread_hwnd;
    task_t *task_queue_head;
    task_t *task_queue_tail;
    struct list timer_list;
} thread_data_t;

thread_data_t *get_thread_data(BOOL);
HWND get_thread_hwnd(void);

LONG get_task_target_magic(void);
void push_task(task_t*,task_proc_t,LONG);
void remove_target_tasks(LONG);

DWORD set_task_timer(HTMLDocument*,DWORD,BOOL,IDispatch*);
HRESULT clear_task_timer(HTMLDocument*,BOOL,DWORD);

const char *debugstr_variant(const VARIANT*);

DEFINE_GUID(CLSID_AboutProtocol, 0x3050F406, 0x98B5, 0x11CF, 0xBB,0x82, 0x00,0xAA,0x00,0xBD,0xCE,0x0B);
DEFINE_GUID(CLSID_JSProtocol, 0x3050F3B2, 0x98B5, 0x11CF, 0xBB,0x82, 0x00,0xAA,0x00,0xBD,0xCE,0x0B);
DEFINE_GUID(CLSID_MailtoProtocol, 0x3050F3DA, 0x98B5, 0x11CF, 0xBB,0x82, 0x00,0xAA,0x00,0xBD,0xCE,0x0B);
DEFINE_GUID(CLSID_ResProtocol, 0x3050F3BC, 0x98B5, 0x11CF, 0xBB,0x82, 0x00,0xAA,0x00,0xBD,0xCE,0x0B);
DEFINE_GUID(CLSID_SysimageProtocol, 0x76E67A63, 0x06E9, 0x11D2, 0xA8,0x40, 0x00,0x60,0x08,0x05,0x93,0x82);

DEFINE_GUID(CLSID_CMarkup,0x3050f4fb,0x98b5,0x11cf,0xbb,0x82,0x00,0xaa,0x00,0xbd,0xce,0x0b);

/* memory allocation functions */

static inline void * __WINE_ALLOC_SIZE(1) heap_alloc(size_t len)
{
    return HeapAlloc(GetProcessHeap(), 0, len);
}

static inline void * __WINE_ALLOC_SIZE(1) heap_alloc_zero(size_t len)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
}

static inline void * __WINE_ALLOC_SIZE(2) heap_realloc(void *mem, size_t len)
{
    return HeapReAlloc(GetProcessHeap(), 0, mem, len);
}

static inline void * __WINE_ALLOC_SIZE(2) heap_realloc_zero(void *mem, size_t len)
{
    return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, mem, len);
}

static inline BOOL heap_free(void *mem)
{
    return HeapFree(GetProcessHeap(), 0, mem);
}

static inline LPWSTR heap_strdupW(LPCWSTR str)
{
    LPWSTR ret = NULL;

    if(str) {
        DWORD size;

        size = (strlenW(str)+1)*sizeof(WCHAR);
        ret = heap_alloc(size);
        memcpy(ret, str, size);
    }

    return ret;
}

static inline LPWSTR heap_strndupW(LPCWSTR str, unsigned len)
{
    LPWSTR ret = NULL;

    if(str) {
        ret = heap_alloc((len+1)*sizeof(WCHAR));
        memcpy(ret, str, len*sizeof(WCHAR));
        ret[len] = 0;
    }

    return ret;
}

static inline char *heap_strdupA(const char *str)
{
    char *ret = NULL;

    if(str) {
        DWORD size;

        size = strlen(str)+1;
        ret = heap_alloc(size);
        memcpy(ret, str, size);
    }

    return ret;
}

static inline WCHAR *heap_strdupAtoW(const char *str)
{
    LPWSTR ret = NULL;

    if(str) {
        DWORD len;

        len = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
        ret = heap_alloc(len*sizeof(WCHAR));
        MultiByteToWideChar(CP_ACP, 0, str, -1, ret, len);
    }

    return ret;
}

static inline char *heap_strdupWtoA(LPCWSTR str)
{
    char *ret = NULL;

    if(str) {
        DWORD size = WideCharToMultiByte(CP_ACP, 0, str, -1, NULL, 0, NULL, NULL);
        ret = heap_alloc(size);
        WideCharToMultiByte(CP_ACP, 0, str, -1, ret, size, NULL, NULL);
    }

    return ret;
}

static inline void windowref_addref(windowref_t *ref)
{
    InterlockedIncrement(&ref->ref);
}

static inline void windowref_release(windowref_t *ref)
{
    if(!InterlockedDecrement(&ref->ref))
        heap_free(ref);
}

HDC get_display_dc(void);
HINSTANCE get_shdoclc(void);
void set_statustext(HTMLDocumentObj*,INT,LPCWSTR);

extern HINSTANCE hInst;
