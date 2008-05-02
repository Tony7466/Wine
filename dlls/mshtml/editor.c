/*
 * Copyright 2006-2007 Jacek Caban for CodeWeavers
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

#include "config.h"

#include <stdarg.h>
#include <stdio.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winnls.h"
#include "ole2.h"
#include "mshtmcid.h"

#include "wine/debug.h"
#include "wine/unicode.h"

#include "mshtml_private.h"
#include "resource.h"

WINE_DEFAULT_DEBUG_CHANNEL(mshtml);

#define NSCMD_ALIGN        "cmd_align"
#define NSCMD_BEGINLINE    "cmd_beginLine"
#define NSCMD_BOLD         "cmd_bold"
#define NSCMD_CHARNEXT     "cmd_charNext"
#define NSCMD_CHARPREVIOUS "cmd_charPrevious"
#define NSCMD_COPY         "cmd_copy"
#define NSCMD_CUT          "cmd_cut"
#define NSCMD_DELETECHARFORWARD   "cmd_deleteCharForward"
#define NSCMD_DELETEWORDFORWARD   "cmd_deleteWordForward"
#define NSCMD_ENDLINE      "cmd_endLine"
#define NSCMD_FONTCOLOR    "cmd_fontColor"
#define NSCMD_FONTFACE     "cmd_fontFace"
#define NSCMD_INDENT       "cmd_indent"
#define NSCMD_INSERTHR     "cmd_insertHR"
#define NSCMD_ITALIC       "cmd_italic"
#define NSCMD_LINENEXT     "cmd_lineNext"
#define NSCMD_LINEPREVIOUS "cmd_linePrevious"
#define NSCMD_MOVEBOTTOM   "cmd_moveBottom"
#define NSCMD_MOVEPAGEDOWN "cmd_movePageDown"
#define NSCMD_MOVEPAGEUP   "cmd_movePageUp"
#define NSCMD_MOVETOP      "cmd_moveTop"
#define NSCMD_OL           "cmd_ol"
#define NSCMD_OUTDENT      "cmd_outdent"
#define NSCMD_PASTE        "cmd_paste"
#define NSCMD_SELECTALL           "cmd_selectAll"
#define NSCMD_SELECTBEGINLINE     "cmd_selectBeginLine"
#define NSCMD_SELECTBOTTOM        "cmd_selectBottom"
#define NSCMD_SELECTCHARNEXT      "cmd_selectCharNext"
#define NSCMD_SELECTCHARPREVIOUS  "cmd_selectCharPrevious"
#define NSCMD_SELECTENDLINE       "cmd_selectEndLine"
#define NSCMD_SELECTLINENEXT      "cmd_selectLineNext"
#define NSCMD_SELECTLINEPREVIOUS  "cmd_selectLinePrevious"
#define NSCMD_SELECTPAGEDOWN      "cmd_selectPageDown"
#define NSCMD_SELECTPAGEUP        "cmd_selectPageUp"
#define NSCMD_SELECTTOP           "cmd_selectTop"
#define NSCMD_SELECTWORDNEXT      "cmd_selectWordNext"
#define NSCMD_SELECTWORDPREVIOUS  "cmd_selectWordPrevious"
#define NSCMD_UL           "cmd_ul"
#define NSCMD_UNDERLINE    "cmd_underline"
#define NSCMD_WORDNEXT     "cmd_wordNext"
#define NSCMD_WORDPREVIOUS "cmd_wordPrevious"

#define NSSTATE_ATTRIBUTE "state_attribute"
#define NSSTATE_ALL       "state_all"

#define NSALIGN_CENTER "center"
#define NSALIGN_LEFT   "left"
#define NSALIGN_RIGHT  "right"

#define DOM_VK_LEFT     VK_LEFT
#define DOM_VK_UP       VK_UP
#define DOM_VK_RIGHT    VK_RIGHT
#define DOM_VK_DOWN     VK_DOWN
#define DOM_VK_DELETE   VK_DELETE
#define DOM_VK_HOME     VK_HOME
#define DOM_VK_END      VK_END

static const WCHAR wszFont[] = {'f','o','n','t',0};
static const WCHAR wszSize[] = {'s','i','z','e',0};

static void do_ns_command(NSContainer *This, const char *cmd, nsICommandParams *nsparam)
{
    nsICommandManager *cmdmgr;
    nsIInterfaceRequestor *iface_req;
    nsresult nsres;

    TRACE("(%p)\n", This);

    nsres = nsIWebBrowser_QueryInterface(This->webbrowser,
            &IID_nsIInterfaceRequestor, (void**)&iface_req);
    if(NS_FAILED(nsres)) {
        ERR("Could not get nsIInterfaceRequestor: %08x\n", nsres);
        return;
    }

    nsres = nsIInterfaceRequestor_GetInterface(iface_req, &IID_nsICommandManager,
                                               (void**)&cmdmgr);
    nsIInterfaceRequestor_Release(iface_req);
    if(NS_FAILED(nsres)) {
        ERR("Could not get nsICommandManager: %08x\n", nsres);
        return;
    }

    nsres = nsICommandManager_DoCommand(cmdmgr, cmd, nsparam, NULL);
    if(NS_FAILED(nsres))
        ERR("DoCommand(%s) failed: %08x\n", debugstr_a(cmd), nsres);

    nsICommandManager_Release(cmdmgr);
}

static void do_ns_editor_command(NSContainer *This, const char *cmd)
{
    nsresult nsres;

    if(!This->editor_controller)
        return;

    nsres = nsIController_DoCommand(This->editor_controller, cmd);
    if(NS_FAILED(nsres))
        ERR("DoCommand(%s) failed: %08x\n", debugstr_a(cmd), nsres);
}

static nsresult get_ns_command_state(NSContainer *This, const char *cmd, nsICommandParams *nsparam)
{
    nsICommandManager *cmdmgr;
    nsIInterfaceRequestor *iface_req;
    nsresult nsres;

    nsres = nsIWebBrowser_QueryInterface(This->webbrowser,
            &IID_nsIInterfaceRequestor, (void**)&iface_req);
    if(NS_FAILED(nsres)) {
        ERR("Could not get nsIInterfaceRequestor: %08x\n", nsres);
        return nsres;
    }

    nsres = nsIInterfaceRequestor_GetInterface(iface_req, &IID_nsICommandManager,
                                               (void**)&cmdmgr);
    nsIInterfaceRequestor_Release(iface_req);
    if(NS_FAILED(nsres)) {
        ERR("Could not get nsICommandManager: %08x\n", nsres);
        return nsres;
    }

    nsres = nsICommandManager_GetCommandState(cmdmgr, cmd, NULL, nsparam);
    if(NS_FAILED(nsres))
        ERR("GetCommandState(%s) failed: %08x\n", debugstr_a(cmd), nsres);

    nsICommandManager_Release(cmdmgr);
    return nsres;
}

static DWORD query_ns_edit_status(HTMLDocument *This, const char *nscmd)
{
    nsICommandParams *nsparam;
    PRBool b = FALSE;

    if(This->usermode != EDITMODE || This->readystate < READYSTATE_INTERACTIVE)
        return OLECMDF_SUPPORTED;

    if(This->nscontainer && nscmd) {
        nsparam = create_nscommand_params();
        get_ns_command_state(This->nscontainer, nscmd, nsparam);

        nsICommandParams_GetBooleanValue(nsparam, NSSTATE_ALL, &b);

        nsICommandParams_Release(nsparam);
    }

    return OLECMDF_SUPPORTED | OLECMDF_ENABLED | (b ? OLECMDF_LATCHED : 0);
}

static void set_ns_align(HTMLDocument *This, const char *align_str)
{
    nsICommandParams *nsparam;

    if(!This->nscontainer)
        return;

    nsparam = create_nscommand_params();
    nsICommandParams_SetCStringValue(nsparam, NSSTATE_ATTRIBUTE, align_str);

    do_ns_command(This->nscontainer, NSCMD_ALIGN, nsparam);

    nsICommandParams_Release(nsparam);
}

static DWORD query_align_status(HTMLDocument *This, const char *align_str)
{
    nsICommandParams *nsparam;
    char *align = NULL;

    if(This->usermode != EDITMODE || This->readystate < READYSTATE_INTERACTIVE)
        return OLECMDF_SUPPORTED;

    if(This->nscontainer) {
        nsparam = create_nscommand_params();
        get_ns_command_state(This->nscontainer, NSCMD_ALIGN, nsparam);

        nsICommandParams_GetCStringValue(nsparam, NSSTATE_ATTRIBUTE, &align);

        nsICommandParams_Release(nsparam);
    }

    return OLECMDF_SUPPORTED | OLECMDF_ENABLED
        | (align && !strcmp(align_str, align) ? OLECMDF_LATCHED : 0);
}


static nsISelection *get_ns_selection(HTMLDocument *This)
{
    nsIDOMWindow *dom_window;
    nsISelection *nsselection = NULL;
    nsresult nsres;

    if(!This->nscontainer)
        return NULL;

    nsres = nsIWebBrowser_GetContentDOMWindow(This->nscontainer->webbrowser, &dom_window);
    if(NS_FAILED(nsres))
        return NULL;

    nsIDOMWindow_GetSelection(dom_window, &nsselection);
    nsIDOMWindow_Release(dom_window);

    return nsselection;

}

static void remove_child_attr(nsIDOMElement *elem, LPCWSTR tag, nsAString *attr_str)
{
    PRBool has_children;
    PRUint32 child_cnt, i;
    nsIDOMNode *child_node;
    nsIDOMNodeList *node_list;
    PRUint16 node_type;

    nsIDOMElement_HasChildNodes(elem, &has_children);
    if(!has_children)
        return;

    nsIDOMElement_GetChildNodes(elem, &node_list);
    nsIDOMNodeList_GetLength(node_list, &child_cnt);

    for(i=0; i<child_cnt; i++) {
        nsIDOMNodeList_Item(node_list, i, &child_node);

        nsIDOMNode_GetNodeType(child_node, &node_type);
        if(node_type == ELEMENT_NODE) {
            nsIDOMElement *child_elem;
            nsAString tag_str;
            const PRUnichar *ctag;

            nsIDOMNode_QueryInterface(child_node, &IID_nsIDOMElement, (void**)&child_elem);

            nsAString_Init(&tag_str, NULL);
            nsIDOMElement_GetTagName(child_elem, &tag_str);
            nsAString_GetData(&tag_str, &ctag, NULL);

            if(!strcmpiW(ctag, tag))
                /* FIXME: remove node if there are no more attributes */
                nsIDOMElement_RemoveAttribute(child_elem, attr_str);

            nsAString_Finish(&tag_str);

            remove_child_attr(child_elem, tag, attr_str);

            nsIDOMNode_Release(child_elem);
        }

        nsIDOMNode_Release(child_node);
    }

    nsIDOMNodeList_Release(node_list);
}

static void get_font_size(HTMLDocument *This, WCHAR *ret)
{
    nsISelection *nsselection = get_ns_selection(This);
    nsIDOMElement *elem = NULL;
    nsIDOMNode *node = NULL, *tmp_node;
    nsAString tag_str;
    LPCWSTR tag;
    PRUint16 node_type;
    nsresult nsres;

    *ret = 0;

    if(!nsselection)
        return;

    nsISelection_GetFocusNode(nsselection, &node);
    nsISelection_Release(nsselection);

    while(node) {
        nsres = nsIDOMNode_GetNodeType(node, &node_type);
        if(NS_FAILED(nsres) || node_type == DOCUMENT_NODE)
            break;

        if(node_type == ELEMENT_NODE) {
            nsIDOMNode_QueryInterface(node, &IID_nsIDOMElement, (void**)&elem);

            nsAString_Init(&tag_str, NULL);
            nsIDOMElement_GetTagName(elem, &tag_str);
            nsAString_GetData(&tag_str, &tag, NULL);

            if(!strcmpiW(tag, wszFont)) {
                nsAString size_str, val_str;
                LPCWSTR val;

                TRACE("found font tag %p\n", elem);

                nsAString_Init(&size_str, wszSize);
                nsAString_Init(&val_str, NULL);

                nsIDOMElement_GetAttribute(elem, &size_str, &val_str);
                nsAString_GetData(&val_str, &val, NULL);

                if(*val) {
                    TRACE("found size %s\n", debugstr_w(val));
                    strcpyW(ret, val);
                }

                nsAString_Finish(&size_str);
                nsAString_Finish(&val_str);
            }

            nsAString_Finish(&tag_str);

            nsIDOMElement_Release(elem);
        }

        if(*ret)
            break;

        tmp_node = node;
        nsIDOMNode_GetParentNode(node, &node);
        nsIDOMNode_Release(tmp_node);
    }

    if(node)
        nsIDOMNode_Release(node);
}

static void set_font_size(HTMLDocument *This, LPCWSTR size)
{
    nsISelection *nsselection;
    PRBool collapsed;
    nsIDOMDocument *nsdoc;
    nsIDOMElement *elem;
    nsIDOMRange *range;
    PRInt32 range_cnt = 0;
    nsAString font_str;
    nsAString size_str;
    nsAString val_str;
    nsresult nsres;

    nsselection = get_ns_selection(This);

    if(!nsselection)
        return;

    nsISelection_GetRangeCount(nsselection, &range_cnt);
    if(range_cnt != 1) {
        FIXME("range_cnt %d not supprted\n", range_cnt);
        if(!range_cnt) {
            nsISelection_Release(nsselection);
            return;
        }
    }

    nsres = nsIWebNavigation_GetDocument(This->nscontainer->navigation, &nsdoc);
    if(NS_FAILED(nsres))
        return;

    nsAString_Init(&font_str, wszFont);
    nsAString_Init(&size_str, wszSize);
    nsAString_Init(&val_str, size);

    nsIDOMDocument_CreateElement(nsdoc, &font_str, &elem);
    nsIDOMElement_SetAttribute(elem, &size_str, &val_str);

    nsISelection_GetRangeAt(nsselection, 0, &range);
    nsISelection_GetIsCollapsed(nsselection, &collapsed);
    nsISelection_RemoveAllRanges(nsselection);

    nsIDOMRange_SurroundContents(range, (nsIDOMNode*)elem);

    if(collapsed) {
        nsISelection_Collapse(nsselection, (nsIDOMNode*)elem, 0);
    }else {
        /* Remove all size attrbutes from the range */
        remove_child_attr(elem, wszFont, &size_str);
        nsISelection_SelectAllChildren(nsselection, (nsIDOMNode*)elem);
    }

    nsIDOMRange_Release(range);
    nsIDOMElement_Release(elem);

    nsAString_Finish(&font_str);
    nsAString_Finish(&size_str);
    nsAString_Finish(&val_str);

    nsISelection_Release(nsselection);
    nsIDOMDocument_Release(nsdoc);
}

static BOOL is_visible_text_node(nsIDOMNode *node)
{
    nsIDOMCharacterData *char_data;
    nsAString data_str;
    LPCWSTR data, ptr;
    PRUint32 len;

    nsIDOMNode_QueryInterface(node, &IID_nsIDOMCharacterData, (void**)&char_data);

    nsIDOMCharacterData_GetLength(char_data, &len);

    nsAString_Init(&data_str, NULL);
    nsIDOMCharacterData_GetData(char_data, &data_str);
    nsAString_GetData(&data_str, &data, NULL);

    if(*data == '\n') {
        len--;
        for(ptr=data+1; ptr && isspaceW(*ptr); ptr++)
            len--;
    }

    nsAString_Finish(&data_str);

    nsIDOMCharacterData_Release(char_data);

    return len != 0;
}

static nsIDOMNode *get_child_text_node(nsIDOMNode *node, BOOL first)
{
    nsIDOMNode *iter, *iter2;

    if(first)
        nsIDOMNode_GetFirstChild(node, &iter);
    else
        nsIDOMNode_GetLastChild(node, &iter);

    while(iter) {
        PRUint16 node_type;

        nsIDOMNode_GetNodeType(iter, &node_type);
        switch(node_type) {
        case TEXT_NODE:
            if(is_visible_text_node(iter))
                return iter;
        case ELEMENT_NODE:
            iter2 = get_child_text_node(iter, first);
            if(iter2) {
                nsIDOMNode_Release(iter);
                return iter2;
            }
        }

        if(first)
            nsIDOMNode_GetNextSibling(iter, &iter2);
        else
            nsIDOMNode_GetPreviousSibling(iter, &iter2);

        nsIDOMNode_Release(iter);
        iter = iter2;
    }

    return NULL;
}

static void handle_arrow_key(HTMLDocument *This, nsIDOMKeyEvent *event, const char * const cmds[4])
{
    int i=0;
    PRBool b;

    nsIDOMKeyEvent_GetCtrlKey(event, &b);
    if(b)
        i |= 1;

    nsIDOMKeyEvent_GetShiftKey(event, &b);
    if(b)
        i |= 2;

    if(cmds[i])
        do_ns_editor_command(This->nscontainer, cmds[i]);

    nsIDOMKeyEvent_PreventDefault(event);
}

void handle_edit_event(HTMLDocument *This, nsIDOMEvent *event)
{
    nsIDOMKeyEvent *key_event;
    PRUint32 code;

    nsIDOMEvent_QueryInterface(event, &IID_nsIDOMKeyEvent, (void**)&key_event);

    nsIDOMKeyEvent_GetKeyCode(key_event, &code);

    switch(code) {
    case DOM_VK_LEFT: {
        static const char * const cmds[] = {
            NSCMD_CHARPREVIOUS,
            NSCMD_WORDPREVIOUS,
            NSCMD_SELECTCHARPREVIOUS,
            NSCMD_SELECTWORDPREVIOUS
        };

        TRACE("left\n");
        handle_arrow_key(This, key_event, cmds);
        break;
    }
    case DOM_VK_RIGHT: {
        static const char * const cmds[] = {
            NSCMD_CHARNEXT,
            NSCMD_WORDNEXT,
            NSCMD_SELECTCHARNEXT,
            NSCMD_SELECTWORDNEXT
        };

        TRACE("right\n");
        handle_arrow_key(This, key_event, cmds);
        break;
    }
    case DOM_VK_UP: {
        static const char * const cmds[] = {
            NSCMD_LINEPREVIOUS,
            NSCMD_MOVEPAGEUP,
            NSCMD_SELECTLINEPREVIOUS,
            NSCMD_SELECTPAGEUP
        };

        TRACE("up\n");
        handle_arrow_key(This, key_event, cmds);
        break;
    }
    case DOM_VK_DOWN: {
        static const char * const cmds[] = {
            NSCMD_LINENEXT,
            NSCMD_MOVEPAGEDOWN,
            NSCMD_SELECTLINENEXT,
            NSCMD_SELECTPAGEDOWN
        };

        TRACE("down\n");
        handle_arrow_key(This, key_event, cmds);
        break;
    }
    case DOM_VK_DELETE: {
        static const char * const cmds[] = {
            NSCMD_DELETECHARFORWARD,
            NSCMD_DELETEWORDFORWARD,
            NULL, NULL
        };

        TRACE("delete\n");
        handle_arrow_key(This, key_event, cmds);
        break;
    }
    case DOM_VK_HOME: {
        static const char * const cmds[] = {
            NSCMD_BEGINLINE,
            NSCMD_MOVETOP,
            NSCMD_SELECTBEGINLINE,
            NSCMD_SELECTTOP
        };

        TRACE("home\n");
        handle_arrow_key(This, key_event, cmds);
        break;
    }
    case DOM_VK_END: {
        static const char * const cmds[] = {
            NSCMD_ENDLINE,
            NSCMD_MOVEBOTTOM,
            NSCMD_SELECTENDLINE,
            NSCMD_SELECTBOTTOM
        };

        TRACE("end\n");
        handle_arrow_key(This, key_event, cmds);
        break;
    }
    }

    nsIDOMKeyEvent_Release(key_event);
}

void handle_edit_load(HTMLDocument *This)
{
    This->nscontainer->editor_controller = get_editor_controller(This->nscontainer);

    if(This->ui_active) {
        OLECHAR wszHTMLDocument[30];
        RECT rcBorderWidths;

        if(This->ip_window)
            IOleInPlaceUIWindow_SetActiveObject(This->ip_window, NULL, NULL);
        if(This->hostui)
            IDocHostUIHandler_HideUI(This->hostui);

        if(This->hostui)
            IDocHostUIHandler_ShowUI(This->hostui, DOCHOSTUITYPE_AUTHOR, ACTOBJ(This), CMDTARGET(This),
                This->frame, This->ip_window);

        LoadStringW(hInst, IDS_HTMLDOCUMENT, wszHTMLDocument,
                    sizeof(wszHTMLDocument)/sizeof(WCHAR));

        if(This->ip_window)
            IOleInPlaceUIWindow_SetActiveObject(This->ip_window, ACTOBJ(This), wszHTMLDocument);

        memset(&rcBorderWidths, 0, sizeof(rcBorderWidths));
        IOleInPlaceFrame_SetBorderSpace(This->frame, &rcBorderWidths);
    }
}

static void set_ns_fontname(NSContainer *This, const char *fontname)
{
    nsICommandParams *nsparam = create_nscommand_params();

    nsICommandParams_SetCStringValue(nsparam, NSSTATE_ATTRIBUTE, fontname);
    do_ns_command(This, NSCMD_FONTFACE, nsparam);
    nsICommandParams_Release(nsparam);
}

static HRESULT exec_delete(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)->(%p %p)\n", This, in, out);

    if(This->nscontainer)
        do_ns_editor_command(This->nscontainer, NSCMD_DELETECHARFORWARD);

    update_doc(This, UPDATE_UI);
    return S_OK;
}

static HRESULT exec_fontname(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)->(%p %p)\n", This, in, out);

    if(!This->nscontainer) {
        update_doc(This, UPDATE_UI);
        return E_FAIL;
    }

    if(in) {
        char *stra;
        DWORD len;

        if(V_VT(in) != VT_BSTR) {
            FIXME("Unsupported vt=%d\n", V_VT(out));
            return E_INVALIDARG;
        }

        TRACE("%s\n", debugstr_w(V_BSTR(in)));

        len = WideCharToMultiByte(CP_ACP, 0, V_BSTR(in), -1, NULL, 0, NULL, NULL);
        stra = mshtml_alloc(len);
        WideCharToMultiByte(CP_ACP, 0, V_BSTR(in), -1, stra, -1, NULL, NULL);

        set_ns_fontname(This->nscontainer, stra);

        mshtml_free(stra);

        update_doc(This, UPDATE_UI);
    }

    if(out) {
        nsICommandParams *nsparam;
        LPWSTR strw;
        char *stra;
        DWORD len;
        nsresult nsres;

        nsparam = create_nscommand_params();

        nsres = get_ns_command_state(This->nscontainer, NSCMD_FONTFACE, nsparam);
        if(NS_FAILED(nsres))
            return S_OK;

        nsICommandParams_GetCStringValue(nsparam, NSSTATE_ATTRIBUTE, &stra);
        nsICommandParams_Release(nsparam);

        len = MultiByteToWideChar(CP_ACP, 0, stra, -1, NULL, 0);
        strw = mshtml_alloc(len*sizeof(WCHAR));
        MultiByteToWideChar(CP_ACP, 0, stra, -1, strw, -1);
        nsfree(stra);

        V_VT(out) = VT_BSTR;
        V_BSTR(out) = SysAllocString(strw);
        mshtml_free(strw);
    }

    return S_OK;
}

static HRESULT exec_forecolor(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)->(%p %p)\n", This, in, out);

    if(in) {
        if(V_VT(in) == VT_I4) {
            nsICommandParams *nsparam = create_nscommand_params();
            char color_str[10];

            sprintf(color_str, "#%02x%02x%02x",
                    V_I4(in)&0xff, (V_I4(in)>>8)&0xff, (V_I4(in)>>16)&0xff);

            nsICommandParams_SetCStringValue(nsparam, NSSTATE_ATTRIBUTE, color_str);
            do_ns_command(This->nscontainer, NSCMD_FONTCOLOR, nsparam);

            nsICommandParams_Release(nsparam);
        }else {
            FIXME("unsupported in vt=%d\n", V_VT(in));
        }

        update_doc(This, UPDATE_UI);
    }

    if(out) {
        FIXME("unsupported out\n");
        return E_NOTIMPL;
    }

    return S_OK;
}

static HRESULT exec_fontsize(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)->(%p %p)\n", This, in, out);

    if(out) {
        WCHAR val[10] = {0};

        get_font_size(This, val);
        V_VT(out) = VT_I4;
        V_I4(out) = strtolW(val, NULL, 10);
    }

    if(in) {
        switch(V_VT(in)) {
        case VT_I4: {
            WCHAR size[10];
            static const WCHAR format[] = {'%','d',0};
            wsprintfW(size, format, V_I4(in));
            set_font_size(This, size);
            break;
        }
        case VT_BSTR:
            set_font_size(This, V_BSTR(in));
            break;
        default:
            FIXME("unsupported vt %d\n", V_VT(in));
        }

        update_doc(This, UPDATE_UI);
    }

    return S_OK;
}

static HRESULT exec_selectall(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)\n", This);

    if(in || out)
        FIXME("unsupported args\n");

    if(This->nscontainer)
        do_ns_command(This->nscontainer, NSCMD_SELECTALL, NULL);

    update_doc(This, UPDATE_UI);
    return S_OK;
}

static HRESULT exec_bold(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)\n", This);

    if(in || out)
        FIXME("unsupported args\n");

    if(This->nscontainer)
        do_ns_command(This->nscontainer, NSCMD_BOLD, NULL);

    update_doc(This, UPDATE_UI);
    return S_OK;
}

static HRESULT exec_italic(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)\n", This);

    if(in || out)
        FIXME("unsupported args\n");

    if(This->nscontainer)
        do_ns_command(This->nscontainer, NSCMD_ITALIC, NULL);

    update_doc(This, UPDATE_UI);
    return S_OK;
}

static HRESULT query_justify(HTMLDocument *This, OLECMD *cmd)
{
    switch(cmd->cmdID) {
    case IDM_JUSTIFYCENTER:
        TRACE("(%p) IDM_JUSTIFYCENTER\n", This);
        cmd->cmdf = query_align_status(This, NSALIGN_CENTER);
        break;
    case IDM_JUSTIFYLEFT:
        TRACE("(%p) IDM_JUSTIFYLEFT\n", This);
        /* FIXME: We should set OLECMDF_LATCHED only if it's set explicitly. */
        if(This->usermode != EDITMODE || This->readystate < READYSTATE_INTERACTIVE)
            cmd->cmdf = OLECMDF_SUPPORTED;
        else
            cmd->cmdf = OLECMDF_SUPPORTED | OLECMDF_ENABLED;
        break;
    case IDM_JUSTIFYRIGHT:
        TRACE("(%p) IDM_JUSTIFYRIGHT\n", This);
        cmd->cmdf = query_align_status(This, NSALIGN_RIGHT);
        break;
    }

    return S_OK;
}

static HRESULT exec_justifycenter(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)\n", This);

    if(in || out)
        FIXME("unsupported args\n");

    set_ns_align(This, NSALIGN_CENTER);
    update_doc(This, UPDATE_UI);
    return S_OK;
}

static HRESULT exec_justifyleft(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)\n", This);

    if(in || out)
        FIXME("unsupported args\n");

    set_ns_align(This, NSALIGN_LEFT);
    update_doc(This, UPDATE_UI);
    return S_OK;
}

static HRESULT exec_justifyright(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)\n", This);

    if(in || out)
        FIXME("unsupported args\n");

    set_ns_align(This, NSALIGN_RIGHT);
    update_doc(This, UPDATE_UI);
    return S_OK;
}

static HRESULT exec_underline(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)\n", This);

    if(in || out)
        FIXME("unsupported args\n");

    if(This->nscontainer)
        do_ns_command(This->nscontainer, NSCMD_UNDERLINE, NULL);

    update_doc(This, UPDATE_UI);
    return S_OK;
}

static HRESULT exec_horizontalline(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)\n", This);

    if(in || out)
        FIXME("unsupported args\n");

    if(This->nscontainer)
        do_ns_command(This->nscontainer, NSCMD_INSERTHR, NULL);

    update_doc(This, UPDATE_UI);
    return S_OK;
}

static HRESULT exec_orderlist(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)\n", This);

    if(in || out)
        FIXME("unsupported args\n");

    if(This->nscontainer)
        do_ns_command(This->nscontainer, NSCMD_OL, NULL);

    update_doc(This, UPDATE_UI);
    return S_OK;
}

static HRESULT exec_unorderlist(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)\n", This);

    if(in || out)
        FIXME("unsupported args\n");

    if(This->nscontainer)
        do_ns_command(This->nscontainer, NSCMD_UL, NULL);

    update_doc(This, UPDATE_UI);
    return S_OK;
}

static HRESULT exec_indent(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)\n", This);

    if(in || out)
        FIXME("unsupported args\n");

    if(This->nscontainer)
        do_ns_command(This->nscontainer, NSCMD_INDENT, NULL);

    update_doc(This, UPDATE_UI);
    return S_OK;
}

static HRESULT exec_outdent(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    TRACE("(%p)\n", This);

    if(in || out)
        FIXME("unsupported args\n");

    if(This->nscontainer)
        do_ns_command(This->nscontainer, NSCMD_OUTDENT, NULL);

    update_doc(This, UPDATE_UI);
    return S_OK;
}

static HRESULT exec_composesettings(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    WCHAR *ptr;

    if(out || !in || V_VT(in) != VT_BSTR) {
        WARN("invalid arg\n");
        return E_INVALIDARG;
    }

    TRACE("(%p)->(%x %s)\n", This, cmdexecopt, debugstr_w(V_BSTR(in)));

    update_doc(This, UPDATE_UI);

    ptr = V_BSTR(in);
    if(*ptr == '1')
        exec_bold(This, cmdexecopt, NULL, NULL);
    ptr = strchrW(ptr, ',');
    if(!ptr)
        return S_OK;

    if(*++ptr == '1')
        exec_italic(This, cmdexecopt, NULL, NULL);
    ptr = strchrW(ptr, ',');
    if(!ptr)
        return S_OK;

    if(*++ptr == '1')
        exec_underline(This, cmdexecopt, NULL, NULL);
    ptr = strchrW(ptr, ',');
    if(!ptr)
        return S_OK;

    if(isdigitW(*++ptr)) {
        VARIANT v;

        V_VT(&v) = VT_I4;
        V_I4(&v) = *ptr-'0';

        exec_fontsize(This, cmdexecopt, &v, NULL);
    }
    ptr = strchrW(ptr, ',');
    if(!ptr)
        return S_OK;

    if(*++ptr != ',')
        FIXME("set font color\n");
    ptr = strchrW(ptr, ',');
    if(!ptr)
        return S_OK;

    if(*++ptr != ',')
        FIXME("set background color\n");
    ptr = strchrW(ptr, ',');
    if(!ptr)
        return S_OK;

    ptr++;
    if(*ptr) {
        VARIANT v;

        V_VT(&v) = VT_BSTR;
        V_BSTR(&v) = SysAllocString(ptr);

        exec_fontname(This, cmdexecopt, &v, NULL);

        SysFreeString(V_BSTR(&v));
    }

    return S_OK;
}

HRESULT editor_exec_copy(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    update_doc(This, UPDATE_UI);

    if(!This->nscontainer)
        return E_FAIL;

    do_ns_editor_command(This->nscontainer, NSCMD_COPY);
    return S_OK;
}

HRESULT editor_exec_cut(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    update_doc(This, UPDATE_UI);

    if(!This->nscontainer)
        return E_FAIL;

    do_ns_editor_command(This->nscontainer, NSCMD_CUT);
    return S_OK;
}

HRESULT editor_exec_paste(HTMLDocument *This, DWORD cmdexecopt, VARIANT *in, VARIANT *out)
{
    update_doc(This, UPDATE_UI);

    if(!This->nscontainer)
        return E_FAIL;

    do_ns_editor_command(This->nscontainer, NSCMD_PASTE);
    return S_OK;
}

static HRESULT query_edit_status(HTMLDocument *This, OLECMD *cmd)
{
    switch(cmd->cmdID) {
    case IDM_DELETE:
        TRACE("CGID_MSHTML: IDM_DELETE\n");
        cmd->cmdf = query_ns_edit_status(This, NULL);
        break;
    case IDM_FONTNAME:
        TRACE("CGID_MSHTML: IDM_FONTNAME\n");
        cmd->cmdf = query_ns_edit_status(This, NULL);
        break;
    case IDM_FONTSIZE:
        TRACE("CGID_MSHTML: IDM_FONTSIZE\n");
        cmd->cmdf = query_ns_edit_status(This, NULL);
        break;
    case IDM_BOLD:
        TRACE("CGID_MSHTML: IDM_BOLD\n");
        cmd->cmdf = query_ns_edit_status(This, NSCMD_BOLD);
        break;
    case IDM_FORECOLOR:
        TRACE("CGID_MSHTML: IDM_FORECOLOR\n");
        cmd->cmdf = query_ns_edit_status(This, NULL);
        break;
    case IDM_ITALIC:
        TRACE("CGID_MSHTML: IDM_ITALIC\n");
        cmd->cmdf = query_ns_edit_status(This, NSCMD_ITALIC);
        break;
    case IDM_UNDERLINE:
        TRACE("CGID_MSHTML: IDM_UNDERLINE\n");
        cmd->cmdf = query_ns_edit_status(This, NSCMD_UNDERLINE);
        break;
    case IDM_HORIZONTALLINE:
        TRACE("CGID_MSHTML: IDM_HORIZONTALLINE\n");
        cmd->cmdf = query_ns_edit_status(This, NULL);
        break;
    case IDM_ORDERLIST:
        TRACE("CGID_MSHTML: IDM_ORDERLIST\n");
        cmd->cmdf = query_ns_edit_status(This, NSCMD_OL);
        break;
    case IDM_UNORDERLIST:
        TRACE("CGID_MSHTML: IDM_HORIZONTALLINE\n");
        cmd->cmdf = query_ns_edit_status(This, NSCMD_UL);
        break;
    case IDM_INDENT:
        TRACE("CGID_MSHTML: IDM_INDENT\n");
        cmd->cmdf = query_ns_edit_status(This, NULL);
        break;
    case IDM_OUTDENT:
        TRACE("CGID_MSHTML: IDM_OUTDENT\n");
        cmd->cmdf = query_ns_edit_status(This, NULL);
        break;
    }

    return S_OK;
}

static HRESULT query_selall_status(HTMLDocument *This, OLECMD *cmd)
{
    TRACE("(%p)->(%p)\n", This, cmd);

    cmd->cmdf = OLECMDF_SUPPORTED|OLECMDF_ENABLED;
    return S_OK;
}

const cmdtable_t editmode_cmds[] = {
    {IDM_DELETE,          query_edit_status,    exec_delete},
    {IDM_FONTNAME,        query_edit_status,    exec_fontname},
    {IDM_FONTSIZE,        query_edit_status,    exec_fontsize},
    {IDM_SELECTALL,       query_selall_status , exec_selectall},
    {IDM_FORECOLOR,       query_edit_status,    exec_forecolor},
    {IDM_BOLD,            query_edit_status,    exec_bold},
    {IDM_ITALIC,          query_edit_status,    exec_italic},
    {IDM_JUSTIFYCENTER,   query_justify,        exec_justifycenter},
    {IDM_JUSTIFYRIGHT,    query_justify,        exec_justifyright},
    {IDM_JUSTIFYLEFT,     query_justify,        exec_justifyleft},
    {IDM_UNDERLINE,       query_edit_status,    exec_underline},
    {IDM_HORIZONTALLINE,  query_edit_status,    exec_horizontalline},
    {IDM_ORDERLIST,       query_edit_status,    exec_orderlist},
    {IDM_UNORDERLIST,     query_edit_status,    exec_unorderlist},
    {IDM_INDENT,          query_edit_status,    exec_indent},
    {IDM_OUTDENT,         query_edit_status,    exec_outdent},
    {IDM_COMPOSESETTINGS, NULL,                 exec_composesettings},
    {0,NULL,NULL}
};

void init_editor(HTMLDocument *This)
{
    update_doc(This, UPDATE_UI);

    if(!This->nscontainer)
        return;

    set_ns_fontname(This->nscontainer, "Times New Roman");
}
