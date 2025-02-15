/*
 * Copyright (C) 2023 Apple Inc. All rights reserved.
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

#pragma once

#include "HTMLElement.h"

namespace WebCore {

class FormAssociatedCustomElement;

class HTMLMaybeFormAssociatedCustomElement final : public HTMLElement {
    WTF_MAKE_ISO_ALLOCATED(HTMLMaybeFormAssociatedCustomElement);
public:
    static Ref<HTMLMaybeFormAssociatedCustomElement> create(const QualifiedName& tagName, Document&);

    using Node::ref;
    using Node::deref;

    bool isMaybeFormAssociatedCustomElement() const final { return true; }
    bool isFormListedElement() const final;
    bool isValidatedFormListedElement() const final;
    bool isFormAssociatedCustomElement() const;

    FormAssociatedElement* asFormAssociatedElement() final;
    FormListedElement* asFormListedElement() final;
    ValidatedFormListedElement* asValidatedFormListedElement() final;
    FormAssociatedCustomElement* formAssociatedCustomElementForElementInternals() const;

    bool matchesValidPseudoClass() const final;
    bool matchesInvalidPseudoClass() const final;
    bool matchesUserValidPseudoClass() const final;
    bool matchesUserInvalidPseudoClass() const final;

    bool supportsFocus() const final;
    bool isLabelable() const final;
    bool isDisabledFormControl() const final;

    void setInterfaceIsFormAssociated();
    bool hasFormAssociatedInterface() const { return hasNodeFlag(NodeFlag::HasFormAssociatedCustomElementInterface); }

    void willUpgradeFormAssociated();
    void didUpgradeFormAssociated();

private:
    HTMLMaybeFormAssociatedCustomElement(const QualifiedName& tagName, Document&);
    virtual ~HTMLMaybeFormAssociatedCustomElement();

    InsertedIntoAncestorResult insertedIntoAncestor(InsertionType, ContainerNode&) final;
    void didFinishInsertingNode() final;
    void didMoveToNewDocument(Document&, Document&) final;
    void removedFromAncestor(RemovalType, ContainerNode&) final;
    void attributeChanged(const QualifiedName&, const AtomString& oldValue, const AtomString& newValue, AttributeModificationReason) final;
    void finishParsingChildren() final;
};

static_assert(sizeof(HTMLMaybeFormAssociatedCustomElement) == sizeof(HTMLElement));

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::HTMLMaybeFormAssociatedCustomElement)
    static bool isType(const WebCore::Element& element) { return element.isMaybeFormAssociatedCustomElement(); }
    static bool isType(const WebCore::Node& node)
    {
        auto* element = dynamicDowncast<WebCore::Element>(node);
        return element && isType(*element);
    }
SPECIALIZE_TYPE_TRAITS_END()
