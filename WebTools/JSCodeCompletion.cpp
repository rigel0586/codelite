#include "JSCodeCompletion.h"
#include "WebToolsConfig.h"
#include "clZipReader.h"
#include "cl_calltip.h"
#include "cl_standard_paths.h"
#include "entry.h"
#include "event_notifier.h"
#include "fileutils.h"
#include "globals.h"
#include "imanager.h"
#include "json_node.h"
#include "macros.h"
#include "navigationmanager.h"
#include "wxCodeCompletionBoxEntry.h"
#include "wxCodeCompletionBoxManager.h"
#include <algorithm>
#include <wx/app.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/stc/stc.h>
#include <wx/xrc/xmlres.h>

#ifdef __WXMSW__
#define ZIP_NAME "javascript-win.zip"
#elif defined(__WXGTK__)
#define ZIP_NAME "javascript.zip"
#else
#define ZIP_NAME "javascript-osx.zip"
#endif

JSCodeCompletion::JSCodeCompletion(const wxString& workingDirectory)
    : m_ternServer(this)
    , m_ccPos(wxNOT_FOUND)
    , m_workingDirectory(workingDirectory)
{
    wxTheApp->Bind(wxEVT_MENU, &JSCodeCompletion::OnGotoDefinition, this, XRCID("ID_MENU_JS_GOTO_DEFINITION"));
    wxFileName jsResources(clStandardPaths::Get().GetDataDir(), ZIP_NAME);
    if(jsResources.Exists()) {

        clZipReader zipReader(jsResources);
        wxFileName targetDir(clStandardPaths::Get().GetUserDataDir(), "");
        targetDir.AppendDir("webtools");
        targetDir.AppendDir("js");

        // Clear the old installation files
        if(targetDir.DirExists()) { targetDir.Rmdir(wxPATH_RMDIR_RECURSIVE); }
        
        // Create the path again and deploy the files
        targetDir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        zipReader.Extract("*", targetDir.GetPath());

        m_ternServer.Start(m_workingDirectory);
    }
}

JSCodeCompletion::~JSCodeCompletion()
{
    m_ternServer.Terminate();
    wxTheApp->Unbind(wxEVT_MENU, &JSCodeCompletion::OnGotoDefinition, this, XRCID("ID_MENU_JS_GOTO_DEFINITION"));
}

bool JSCodeCompletion::SanityCheck()
{
    wxFileName nodeJS;
    if(!clTernServer::LocateNodeJS(nodeJS)) {
        wxString msg;
        msg << _(
            "It seems that NodeJS is not installed on your machine\nI have temporarily disabled Code Completion for "
            "JavaScript\nPlease install "
            "NodeJS and try again");
        wxMessageBox(msg, "CodeLite", wxICON_WARNING | wxOK | wxCENTER);

        // Disable CC
        WebToolsConfig& conf = WebToolsConfig::Get();
        conf.EnableJavaScriptFlag(WebToolsConfig::kJSEnableCC, false);
        return false;
    }
    return true;
}

void JSCodeCompletion::CodeComplete(IEditor* editor)
{
    if(!IsEnabled()) {
        TriggerWordCompletion();
        return;
    }

    if(!SanityCheck()) return;

    // Sanity
    CHECK_PTR_RET(editor);

    // Check the completion type
    wxStyledTextCtrl* ctrl = editor->GetCtrl();
    int currentPos = ctrl->PositionBefore(ctrl->GetCurrentPos());
    bool isFunctionTip = false;
    while(currentPos > 0) {
        char prevChar = ctrl->GetCharAt(currentPos);
        if((prevChar == ' ') || (prevChar == '\t') || (prevChar == '\n') || (prevChar == '\r')) {
            currentPos = ctrl->PositionBefore(currentPos);
            continue;
        }
        if(prevChar == '(') { isFunctionTip = true; }
        break;
    }

    m_ccPos = ctrl->GetCurrentPos();
    if(!isFunctionTip) {
        m_ternServer.PostCCRequest(editor);

    } else {
        m_ternServer.PostFunctionTipRequest(editor, currentPos);
    }
}

void JSCodeCompletion::OnCodeCompleteReady(const wxCodeCompletionBoxEntry::Vec_t& entries, const wxString& filename)
{
    IEditor* editor = ::clGetManager()->GetActiveEditor();

    // sanity
    CHECK_PTR_RET(editor);
    if(editor->GetFileName().GetFullPath() != filename) return;
    if(editor->GetCurrentPosition() != m_ccPos) return;
    if(entries.empty()) {
        TriggerWordCompletion();
        return;
    }

    wxStyledTextCtrl* ctrl = editor->GetCtrl();
    wxCodeCompletionBoxManager::Get().ShowCompletionBox(ctrl, entries, 0, wxNOT_FOUND, this);
}

void JSCodeCompletion::OnFunctionTipReady(clCallTipPtr calltip, const wxString& filename)
{
    IEditor* editor = ::clGetManager()->GetActiveEditor();

    // sanity
    CHECK_PTR_RET(editor);
    CHECK_PTR_RET(calltip);
    if(editor->GetFileName().GetFullPath() != filename) return;
    if(editor->GetCurrentPosition() != m_ccPos) return;
    editor->ShowCalltip(calltip);
}

void JSCodeCompletion::Reload() { m_ternServer.RecycleIfNeeded(true); }

bool JSCodeCompletion::IsEnabled() const
{
    WebToolsConfig& conf = WebToolsConfig::Get();
    return conf.HasJavaScriptFlag(WebToolsConfig::kJSEnableCC);
}

void JSCodeCompletion::TriggerWordCompletion()
{
    // trigger word completion
    wxCommandEvent wordCompleteEvent(wxEVT_MENU, XRCID("simple_word_completion"));
    EventNotifier::Get()->TopFrame()->GetEventHandler()->AddPendingEvent(wordCompleteEvent);
}

void JSCodeCompletion::FindDefinition(IEditor* editor)
{
    if(!IsEnabled()) { return; }

    if(!SanityCheck()) return;

    // Sanity
    CHECK_PTR_RET(editor);

    wxStyledTextCtrl* ctrl = editor->GetCtrl();
    m_ccPos = ctrl->GetCurrentPos();
    m_ternServer.PostFindDefinitionRequest(editor);
}

void JSCodeCompletion::OnDefinitionFound(const clTernDefinition& loc)
{
    if(loc.IsURL()) {
        ::wxLaunchDefaultBrowser(loc.url);
    } else {
        BrowseRecord from, to;
        wxString pattern;
        if(clGetManager()->GetActiveEditor()) {
            pattern = clGetManager()->GetActiveEditor()->GetWordAtCaret();
            from = clGetManager()->GetActiveEditor()->CreateBrowseRecord();
        }
        IEditor* editor = clGetManager()->OpenFile(loc.file);
        if(editor) {
            editor->CenterLine(editor->LineFromPos(loc.start));
            if(editor->FindAndSelect(pattern, pattern, loc.start, NULL)) {
                to = editor->CreateBrowseRecord();
                // Record this jump
                clGetManager()->GetNavigationMgr()->AddJump(from, to);
            }
        }
    }
}

void JSCodeCompletion::ResetTern()
{
    if(!IsEnabled()) { return; }

    if(!SanityCheck()) return;

    // Sanity
    m_ccPos = wxNOT_FOUND;

    // recycle tern
    // m_ternServer.PostResetCommand(true);
    m_ternServer.RecycleIfNeeded(true);
}

void JSCodeCompletion::AddContextMenu(wxMenu* menu, IEditor* editor)
{
    wxUnusedVar(editor);
    menu->PrependSeparator();
    menu->Prepend(XRCID("ID_MENU_JS_GOTO_DEFINITION"), _("Find Definition"));
}

void JSCodeCompletion::OnGotoDefinition(wxCommandEvent& event)
{
    wxUnusedVar(event);
    FindDefinition(clGetManager()->GetActiveEditor());
}

void JSCodeCompletion::ReparseFile(IEditor* editor)
{
    if(!IsEnabled()) { return; }
    CHECK_PTR_RET(editor);

    if(!SanityCheck()) return;

    // Sanity
    m_ccPos = wxNOT_FOUND;
    m_ternServer.PostReparseCommand(editor);
}
