/****************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the QtScript module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL-ONLY$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QSCRIPTCONTEXT_IMPL_P_H
#define QSCRIPTCONTEXT_IMPL_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

QT_BEGIN_NAMESPACE

#include "qscriptcontext_p.h"
#include "qscriptvalue_p.h"
#include "qscriptengine_p.h"

inline QScriptContextPrivate::QScriptContextPrivate(QScriptEnginePrivate *engine)
    : q_ptr(this), engine(engine), arguments(0), accessorInfo(0), parent(engine->setCurrentQSContext(this)), previous(0)
{
    Q_ASSERT(engine);
}

inline QScriptContextPrivate::QScriptContextPrivate(QScriptEnginePrivate *engine, const v8::Arguments *args)
    : q_ptr(this), engine(engine), arguments(args), accessorInfo(0),
      context(v8::Persistent<v8::Context>::New(v8::Context::NewFunctionContext())),
      parent(engine->setCurrentQSContext(this)), previous(0)
{
    Q_ASSERT(engine);
    context->Enter();
}

inline QScriptContextPrivate::QScriptContextPrivate(QScriptEnginePrivate *engine, const v8::AccessorInfo *accessor)
: q_ptr(this), engine(engine), arguments(0), accessorInfo(accessor),
  context(v8::Persistent<v8::Context>::New(v8::Context::NewFunctionContext())),
  parent(engine->setCurrentQSContext(this)), previous(0)
{
    Q_ASSERT(engine);
    context->Enter();
}

inline QScriptContextPrivate::QScriptContextPrivate(QScriptEnginePrivate *engine, v8::Handle<v8::Context> context)
    : q_ptr(this), engine(engine), arguments(0), accessorInfo(0),
      context(v8::Persistent<v8::Context>::New(context)), parent(engine->setCurrentQSContext(this)),
      previous(0)
{
    Q_ASSERT(engine);
    context->Enter();
}

inline QScriptContextPrivate::QScriptContextPrivate(QScriptContextPrivate *parent, v8::Handle<v8::StackFrame> frame)
    : q_ptr(this), engine(parent->engine), arguments(0), accessorInfo(0),
      parent(parent), previous(0), frame(v8::Persistent<v8::StackFrame>::New(frame))
{
    Q_ASSERT(engine);
}


inline QScriptContextPrivate::~QScriptContextPrivate()
{
    Q_ASSERT(engine);
    if (previous)
        delete previous;

    m_thisObject.Dispose();

    if (!parent)
        return;

    if (frame.IsEmpty()) {
        QScriptContextPrivate *old = engine->setCurrentQSContext(parent);
        if (old != this) {
            qWarning("QScriptEngine::pushContext() doesn't match with popContext()");
            old->parent = 0;
            //old is most likely leaking.
        }
    } else {
        frame.Dispose();
    }

    while (!scopes.isEmpty())
        QScriptValuePrivate::get(popScope());

    inheritedScope.Dispose();
    if (!context.IsEmpty()) {
        context->Exit();
        context.Dispose();
    }
}

inline QScriptPassPointer<QScriptValuePrivate> QScriptContextPrivate::argument(int index) const
{
    if (index < 0)
        return new QScriptValuePrivate();

    if (arguments) {
        if (index >= arguments->Length())
            return new QScriptValuePrivate(engine, QScriptValue::UndefinedValue);

        return new QScriptValuePrivate(engine, (*arguments)[index]);
    }

    Q_UNIMPLEMENTED();
    return new QScriptValuePrivate();
}

inline int QScriptContextPrivate::argumentCount() const
{
    if (arguments) {
        return arguments->Length();
    }

    Q_UNIMPLEMENTED();
    return 0;
}

inline QScriptPassPointer<QScriptValuePrivate> QScriptContextPrivate::argumentsObject() const
{
    if (arguments) {
        if (!argsObject) {
            QScriptContextPrivate *that = const_cast<QScriptContextPrivate *>(this);
            that->argsObject = that->createArgumentsObject();
        }
        return argsObject.data();
    }

    Q_UNIMPLEMENTED();
    return new QScriptValuePrivate();
}

inline v8::Handle<v8::Object> QScriptContextPrivate::thisObject() const
{
    if (arguments) {
        return arguments->This();
    } else if (accessorInfo) {
        return accessorInfo->This();
    } else if (!m_thisObject.IsEmpty()) {
        return m_thisObject;
    } else {
        return engine->globalObject();
    }
}

inline void QScriptContextPrivate::setThisObject(QScriptValuePrivate *newThis)
{
    if (!newThis->isObject()) {
//        qWarning() << "QScriptContext::setThisObject: cannot set this object for native context";
        return;
    }
    if (newThis->engine() != engine) {
        qWarning("QScriptContext::setThisObject() failed: cannot set an object created in "
                 "a different engine");
        return;
    }
    if (!parent) {
        //global object
        engine->setGlobalObject(newThis);
        return;
    }
    if (!frame.IsEmpty())
        qWarning() << "QScriptContext::setThisObject: cannot set this object for native context";
    m_thisObject.Dispose();
    m_thisObject = v8::Persistent<v8::Object>::New(*newThis);
}


inline QScriptPassPointer<QScriptValuePrivate> QScriptContextPrivate::callee() const
{
    if (arguments)
        return new QScriptValuePrivate(engine, arguments->Callee());

    Q_UNIMPLEMENTED();
    return new QScriptValuePrivate();
}

inline QScriptPassPointer<QScriptValuePrivate> QScriptContextPrivate::activationObject() const
{
    if (!parent)
        return new QScriptValuePrivate(engine, engine->globalObject());
    if (context.IsEmpty()) {
        Q_UNIMPLEMENTED();
        return new QScriptValuePrivate();
    }
    Q_ASSERT(!context.IsEmpty());
    return new QScriptValuePrivate(engine, context->GetExtensionObject());
}

inline QScriptValueList QScriptContextPrivate::scopeChain() const
{
    QScriptValueList list;
    for (int i = 0; i < scopes.size(); ++i) {
        v8::Handle<v8::Object> object = scopes.at(i)->GetExtensionObject();
        list.append(QScriptValuePrivate::get(new QScriptValuePrivate(engine, object)));
    }

    if (!context.IsEmpty())
        list.append(QScriptValuePrivate::get(activationObject()));

    if (!inheritedScope.IsEmpty()) {
        v8::Handle<v8::Context> current = inheritedScope;
        do {
            v8::Handle<v8::Object> object = current->GetExtensionObject();
            list.append(QScriptValuePrivate::get(new QScriptValuePrivate(engine, object)));
            current = current->GetPrevious();
        } while (!current.IsEmpty());
    }

    if (parent) {
        // Implicit global context
        list.append(QScriptValuePrivate::get(new QScriptValuePrivate(engine, engine->globalObject())));
    }

    return list;
}

inline void QScriptContextPrivate::pushScope(QScriptValuePrivate *object)
{
    v8::Handle<v8::Object> objectHandle(v8::Object::Cast(*object->asV8Value(engine)));
    v8::Handle<v8::Context> scopeContext = v8::Context::NewScopeContext(objectHandle);
    scopes.append(v8::Persistent<v8::Context>::New(scopeContext));
    scopeContext->Enter();
}

inline QScriptPassPointer<QScriptValuePrivate> QScriptContextPrivate::popScope()
{
    if (scopes.isEmpty()) {
        // In the old back-end, this would pop the activation object
        // from the scope chain.
        Q_UNIMPLEMENTED();
        return new QScriptValuePrivate();
    }
    v8::Persistent<v8::Context> scopeContext = scopes.takeFirst();
    v8::Handle<v8::Object> object = scopeContext->GetExtensionObject();
    scopeContext->Exit();
    scopeContext.Dispose();
    return new QScriptValuePrivate(engine, object);
}

inline void QScriptContextPrivate::setInheritedScope(v8::Handle<v8::Context> object)
{
    Q_ASSERT(inheritedScope.IsEmpty());
    inheritedScope = v8::Persistent<v8::Context>::New(object);
}

inline QScriptPassPointer<QScriptValuePrivate> QScriptContextPrivate::createArgumentsObject()
{
    Q_ASSERT(arguments);

    // Create a fake arguments object.
    // TODO: Get the real one from v8, if possible.
    int argc = argumentCount();
    QScriptPassPointer<QScriptValuePrivate> args = engine->newObject(); // ECMA says it's an Object
    for (int i = 0; i < argc; ++i) {
        QScriptValue arg = QScriptValuePrivate::get(argument(i));
        args->setProperty(i, QScriptValuePrivate::get(arg), v8::DontEnum);
    }
    QScriptValue callee_ = QScriptValuePrivate::get(callee());
    args->setProperty(QString::fromLatin1("length"), new QScriptValuePrivate(engine, argc), v8::DontEnum);
    args->setProperty(QString::fromLatin1("callee"), QScriptValuePrivate::get(callee_), v8::DontEnum);
    return args;
}

static v8::Handle<v8::Value> argumentsPropertyGetter(v8::Local<v8::String> property, const v8::AccessorInfo &info)
{
    v8::Local<v8::Object> self = info.Holder();
    QScriptContextPrivate *ctx = static_cast<QScriptContextPrivate *>(v8::External::Unwrap(info.Data()));

    QScriptSharedDataPointer<QScriptValuePrivate> argsObject(ctx->argumentsObject());
    self->ForceSet(property, *argsObject);
    return *argsObject;
}

inline void QScriptContextPrivate::initializeArgumentsProperty()
{
    Q_ASSERT(arguments);

    // Since this is in the hotpath for QScriptEngine::evaluate(), cut
    // some corners and access "extension object" via v8 directly
    // instead of using activationObject().

    v8::Handle<v8::Object> activation = context->GetExtensionObject();
    if (activation->Has(v8::String::New("arguments")))
        return;

    // If the argsObject wasn't created yet, we just add an accessor
    if (!argsObject)
        activation->SetAccessor(v8::String::New("arguments"), argumentsPropertyGetter, 0, v8::External::Wrap(this));
    else
        activation->Set(v8::String::New("arguments"), *argsObject);
}

inline v8::Handle<v8::Value> QScriptContextPrivate::throwError(QScriptContext::Error error, const QString& text)
{
    v8::Handle<v8::String> message = QScriptConverter::toString(text);
    v8::Handle<v8::Value> exception;
    switch (error) {
        case UnknownError:
            exception = v8::Exception::Error(message);
            break;
        case ReferenceError:
            exception = v8::Exception::ReferenceError(message);
            break;
        case SyntaxError:
            exception = v8::Exception::SyntaxError(message);
            break;
        case TypeError:
            exception = v8::Exception::TypeError(message);
            break;
        case RangeError:
            exception = v8::Exception::RangeError(message);
            break;
        case URIError:
            //FIXME
            exception = v8::Exception::Error(message);
            break;
    }
    return engine->throwException(exception);
}


QT_END_NAMESPACE

#endif