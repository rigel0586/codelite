#include "LSP/DidChangeTextDocumentRequest.h"

LSP::DidChangeTextDocumentRequest::DidChangeTextDocumentRequest(const wxFileName& filename, const wxString& fileContent)
{
    thread_local int counter = 0;
    SetMethod("textDocument/didChange");
    m_params.reset(new DidChangeTextDocumentParams());

    VersionedTextDocumentIdentifier id;
    id.SetVersion(++counter);
    id.SetFilename(filename);
    m_params->As<DidChangeTextDocumentParams>()->SetTextDocument(id);

    TextDocumentContentChangeEvent changeEvent;
    changeEvent.SetText(fileContent);
    m_params->As<DidChangeTextDocumentParams>()->SetContentChanges({ changeEvent });
}

LSP::DidChangeTextDocumentRequest::~DidChangeTextDocumentRequest() {}
