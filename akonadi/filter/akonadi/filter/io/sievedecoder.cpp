/****************************************************************************** *
 *
 *  File : sievedecoder.cpp
 *  Created on Sun 03 May 2009 12:10:16 by Szymon Tomasz Stefanek
 *
 *  This file is part of the Akonadi Filtering Framework
 *
 *  Copyright 2009 Szymon Tomasz Stefanek <pragma@kvirc.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *******************************************************************************/

#include "sievedecoder.h"

#include "sievereader.h"

#include "../componentfactory.h"
#include "../program.h"
#include "../rule.h"
#include "../condition.h"
#include "../rulelist.h"
#include "../functiondescriptor.h"

#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QByteArray>
#include <QtCore/QUrl>

#include <KDebug>
#include <KLocale>

#define DEBUG_SIEVE_DECODER 1

// Sieve is an ugly language. It's hard to parse, has some ambiguities,
// humans have trouble understanding its constructs and in the end it's very hard
// to provide a "nice" object-oriented rappresentation of its structure. We actually support
// a subset of the whole language and we extend it in other directions to support
// constructs which the original specification is unable to encode.

namespace Akonadi
{
namespace Filter
{
namespace IO
{

SieveDecoder::SieveDecoder( ComponentFactory * componentFactory )
  : Decoder( componentFactory ), mProgram( 0 ), mCreationOfCustomDataMemberDescriptorsEnabled( true )
{
}

SieveDecoder::~SieveDecoder()
{
  if( mProgram )
    delete mProgram;
}

void SieveDecoder::pushError( const QString &error )
{
  errorStack().pushError( i18n( "line %1", mLineNumber ), error );
}


void SieveDecoder::onError( const QString & error )
{
  mGotError = true;
  pushError( error );
}

bool SieveDecoder::addConditionToCurrentComponent( Condition::Base * condition, const QString &identifier )
{
  if( mCurrentComponent->isRule() )
  {
    Rule * rule = static_cast< Rule * >( mCurrentComponent );
    if ( rule->condition() )
    {
      delete condition;
      mGotError = true; 
      pushError( i18n( "Unexpected start of test '%1' after a previous test", identifier ) );
      return false;  
    }
    rule->setCondition( condition );
    mCurrentComponent = condition; // always becomes current
    return true;
  }

  if( mCurrentComponent->isCondition() )
  {
    // hm
    switch( static_cast< Condition::Base *>( mCurrentComponent )->conditionType() )
    {
      case Condition::ConditionTypeAnd:
      case Condition::ConditionTypeOr:
      {
        Condition::Multi * multi = static_cast< Condition::Multi * >( mCurrentComponent );
        multi->addChildCondition( condition );
        mCurrentComponent = condition;
        return true;
      }
      break;
      case Condition::ConditionTypeNot:
      {
        Condition::Not * whoTheHeckDefinedNotToTheExclamationMark = static_cast< Condition::Not * >( mCurrentComponent );
        Condition::Base * currentNotChild = whoTheHeckDefinedNotToTheExclamationMark->childCondition();
        if( currentNotChild )
        {
          // ugly.. the not alreay has a child condition: it should have.
          // we fix this by creating an inner "or"
          whoTheHeckDefinedNotToTheExclamationMark->releaseChildCondition();
          Condition::Or * orCondition = componentFactory()->createOrCondition( mCurrentComponent );
          Q_ASSERT( orCondition );
          whoTheHeckDefinedNotToTheExclamationMark->setChildCondition( orCondition );
          orCondition->addChildCondition( currentNotChild );
          orCondition->addChildCondition( condition );
        } else {
          whoTheHeckDefinedNotToTheExclamationMark->setChildCondition( condition );
        }
        mCurrentComponent = condition;
        return true;
      }
      break;
      default:
        // unexpected
      break;
    }
  }

  delete condition;

  mGotError = true;
  pushError( i18n( "Unexpected start of test '%1' outside of a rule or a condition", identifier ) );
  return false;
}

void SieveDecoder::onTestStart( const QString & identifier )
{
  if( mGotError )
    return; // ignore everything

  if( !mCurrentSimpleTestName.isEmpty() )
  {
    // a test can't start inside a simple test
    mGotError = true; 
    pushError( i18n( "Unexpected start of test '%1' inside a simple test", identifier ) );
    return;  
  }

  Condition::Base * condition = 0;

  if( QString::compare( identifier, QLatin1String( "allof" ), Qt::CaseInsensitive ) == 0 )
    condition = componentFactory()->createAndCondition( mCurrentComponent );
  else if( QString::compare( identifier, QLatin1String( "anyof" ), Qt::CaseInsensitive ) == 0 )
    condition = componentFactory()->createOrCondition( mCurrentComponent );
  else if( QString::compare( identifier, QLatin1String( "not" ), Qt::CaseInsensitive ) == 0 )
    condition = componentFactory()->createNotCondition( mCurrentComponent );
  else if( QString::compare( identifier, QLatin1String( "true" ), Qt::CaseInsensitive ) == 0 )
    condition = componentFactory()->createTrueCondition( mCurrentComponent );
  else if( QString::compare( identifier, QLatin1String( "false" ), Qt::CaseInsensitive ) == 0 )
    condition = componentFactory()->createFalseCondition( mCurrentComponent );
  else {
    // we delay the start of the simple tests to "onTestEnd", as they can't be structured.
    mCurrentSimpleTestName = identifier;
    mCurrentSimpleTestArguments.clear();
    return;
  }

  mCurrentSimpleTestName = QString(); // this is not a simple test

  Q_ASSERT( condition );

  addConditionToCurrentComponent( condition, identifier );
}

void SieveDecoder::onTestEnd()
{
  if( mGotError )
    return; // ignore everything

  if( !mCurrentSimpleTestName.isEmpty() )
  {
    // this is the end of a simple test

    // here you have most of the weirdness of the sieve syntax

    // standard sieve syntax
    //
    //   testname {:<modifier> {<modifierArgument>}} [:<operator>] {:<modifier> {<modifierArgument>}} [ field_list ] [ value_list ]
    //

    // kill the ugly modifiers we don't support and split the arguments in tagged and not tagged
    // FIXME: we should take care of the non tagged arguments that live "in between" the tagged ones: these should be discarded too!

    QString arg;

    QList< QString > taggedArguments;
    QList< QVariant > nonTaggedArguments;

    QList< QVariant >::Iterator it = mCurrentSimpleTestArguments.begin();
    while( it != mCurrentSimpleTestArguments.end() )
    {
      if( ( *it ).type() != QVariant::String )
      {
        nonTaggedArguments.append( *it );
        ++it;
        continue;
      }

      arg = ( *it ).toString();

      if( arg.length() < 1 )
      {
        nonTaggedArguments.append( *it );
        ++it;
        continue;
      }

      if( arg.at( 0 ) != QChar(':') )
      {
        nonTaggedArguments.append( *it );
        ++it;
        continue; // not tagged
      }

      if( QString::compare( arg, QString::fromAscii(":comparator"), Qt::CaseInsensitive ) == 0 )
      {
        // kill it, and the following argument too
        ++it;
        if( it != mCurrentSimpleTestArguments.end() )
          ++it;
        continue;
      }

      if( QString::compare( arg, QString::fromAscii(":content"), Qt::CaseInsensitive ) == 0 )
      {
        // kill it, and the following argument too
        ++it;
        if( it != mCurrentSimpleTestArguments.end() )
          ++it;
        continue;
      }

      if( QString::compare( arg, QString::fromAscii(":all"), Qt::CaseInsensitive ) == 0 )
      {
        // bleah.. this is the default
        ++it;
        continue;
      }

      if( QString::compare( arg, QString::fromAscii(":raw"), Qt::CaseInsensitive ) == 0 )
      {
        ++it;
        continue;
      }

      if( QString::compare( arg, QString::fromAscii(":text"), Qt::CaseInsensitive ) == 0 )
      {
        ++it;
        continue;
      }

      taggedArguments.append( arg.mid( 1 ) );
      ++it;
    }

    mCurrentSimpleTestArguments.clear();

    // now play with the tagged arguments: the last one should be the operator (if any)

    QVariant argVariant;
    QString operatorKeyword;
    QString functionKeyword = mCurrentSimpleTestName;

    if( taggedArguments.count() > 0 )
    {
      operatorKeyword = taggedArguments.last();
      taggedArguments.removeLast();

      foreach( arg, taggedArguments )
      {
        functionKeyword += QChar(':');
        functionKeyword += arg;
      }
    }

    // lookup the function
    const FunctionDescriptor * function = componentFactory()->findFunction( functionKeyword );
    if ( !function )
    {
      // unrecognized test
      mGotError = true; 
      pushError( i18n( "Unrecognized function '%1'", functionKeyword ) );
      return;
    }

    if( operatorKeyword.isEmpty() )
    {
      // FIXME: Use a default "forwarding" operator that accepts only boolean results ?
      kDebug() << "OperatorDescriptor keyword is empty in function" << functionKeyword;
    }

    // look up the operator

    const OperatorDescriptor * op = 0;

    op = componentFactory()->findOperator( operatorKeyword, function->outputDataTypeMask() );

    if( !op )
    {
      // unrecognized test
      mGotError = true;
      pushError( i18n( "Unrecognized operator '%1' for function '%2'", operatorKeyword, functionKeyword ) );
      return;
    }

    // now we may or may not have additional arguments: [field_list] [value_list]
    // if the operator *requires* a value then we *must* have it.
    QVariant values;
    QVariant fields;

    if( op->rightOperandDataType() != DataTypeNone )
    {
      // we expect a right operand (list)
      if( nonTaggedArguments.isEmpty() )
      {
        mGotError = true;
        pushError( i18n( "The operator '%1' expects a value argument", operatorKeyword ) );
        return;
      }

      values = nonTaggedArguments.last();
      nonTaggedArguments.removeLast();
    } else {
      // we don't expect a right operand
    }

    // now we may or may not have an argument (field).
    // the empty field is considered as "item" (whole item, in most cases)

    if( !nonTaggedArguments.isEmpty() )
      fields = nonTaggedArguments.last();

    QStringList fieldList;

    if ( !fields.isNull() )
    {
      if( !qVariantCanConvert< QStringList >( fields ) )
      {
        mGotError = true;
        pushError( i18n( "The field name argument must be convertible to a string list" ) );
        return;
      }

      fieldList = fields.toStringList();
    } else {
      // use a single "item" data member (the whole item)
      fieldList.append( "item" );
    }
    

    QList< QVariant > valueList;

    if( values.isNull() )
    {
       valueList.append( values );
    } else {
      switch( values.type() )
      {
        case QVariant::ULongLong:
          valueList.append( values );
        break;
        case QVariant::String:
          valueList.append( values );
        break;
        case QVariant::StringList:
        {
          QStringList sl = values.toStringList();
          foreach( QString stringValue, sl )
            valueList.append( QVariant( stringValue ) );
        }
        break;
        default:
          Q_ASSERT( false ); // shouldn't happen
        break;
      }
    }

    bool multiTest = false;

    // if there is more than one field or more than one value then we use an "or" test as parent
    if( ( valueList.count() > 1 ) || ( fieldList.count() > 1 ) )
    {
      Condition::Or * orCondition = componentFactory()->createOrCondition( mCurrentComponent );
      Q_ASSERT( orCondition );

      if( !addConditionToCurrentComponent( orCondition, QLatin1String( "anyof" ) ) )
        return; // error already signaled

      multiTest = true;
    }

    foreach( QString field, fieldList )
    {
      Q_ASSERT( !field.isEmpty() );

      foreach( QVariant value, valueList )
      {
        const DataMemberDescriptor * dataMember = componentFactory()->findDataMember( field );
        if( !dataMember )
        {
          if( ( !mCreationOfCustomDataMemberDescriptorsEnabled ) || ( !( function->acceptableInputDataTypeMask() & DataTypeString ) ) )
          {
            mGotError = true;
            pushError( i18n( "Test on data member '%1' is not supported.", field ) );
            return;
          }

          QString name = i18n( "the field '%1'", field );
          DataMemberDescriptor * newDataMemberDescriptor = new DataMemberDescriptor( DataMemberCustomFirst, field, name, DataTypeString );
          componentFactory()->registerDataMember( newDataMemberDescriptor );
          dataMember = newDataMemberDescriptor;
        }

        Q_ASSERT( dataMember );

        if( !( dataMember->dataType() & function->acceptableInputDataTypeMask() ) )
        {
          mGotError = true; 
          pushError( i18n( "Function '%1' can't be applied to data member '%2'.", function->keyword(), field ) );
          return;
        }

        Condition::Base * condition = componentFactory()->createPropertyTestCondition( mCurrentComponent, function, dataMember, op, value );
        if ( !condition )
        {
          // unrecognized test
          mGotError = true; 
          pushError( i18n( "Test on function '%1' is not supported: %2", field, componentFactory()->lastError() ) );
          return;
        }

        if( !addConditionToCurrentComponent( condition, mCurrentSimpleTestName ) )
          return; // error already set

        if( multiTest )
        {
          // pop it (so the current condition becomes the or added above)

          mCurrentComponent = mCurrentComponent->parent();
          Q_ASSERT( mCurrentComponent ); // a condition always has a parent
          Q_ASSERT( mCurrentComponent->isCondition() );
        }
      }
    }

    mCurrentSimpleTestName = QString();
  }

  if( !mCurrentComponent->isCondition() )
  {
    mGotError = true;
    pushError( i18n( "Unexpected end of test outside of a condition" ) );
    return;
  }

  // pop
  mCurrentComponent = mCurrentComponent->parent();
  Q_ASSERT( mCurrentComponent ); // a condition always has a parent
  Q_ASSERT( mCurrentComponent->isCondition() || mCurrentComponent->isRule() );
}

void SieveDecoder::onTaggedArgument( const QString & tag )
{
  if( mGotError )
    return; // ignore everything

  if( !mCurrentSimpleCommandName.isEmpty() )
  {
    Q_ASSERT( mCurrentSimpleTestName.isEmpty() );
    // argument to a simple command
    mCurrentSimpleCommandArguments.append( QVariant( tag ) );
    return;
  }

  if( !mCurrentSimpleTestName.isEmpty() )
  {
    // argument to a simple test
    QString bleah = QChar( ':' );
    bleah += tag;
    mCurrentSimpleTestArguments.append( QVariant( bleah ) );
    return;
  }

  mGotError = true;
  pushError( i18n( "Unexpected tagged argument outside of a simple test or simple command" ) );
}

void SieveDecoder::onStringArgument( const QString & string, bool multiLine, const QString & embeddedHashComment )
{
  Q_UNUSED( multiLine );
  Q_UNUSED( embeddedHashComment );

  if( mGotError )
    return; // ignore everything

  if( !mCurrentSimpleCommandName.isEmpty() )
  {
    Q_ASSERT( mCurrentSimpleTestName.isEmpty() );
    // argument to a simple command
    mCurrentSimpleCommandArguments.append( QVariant( string ) );
    return;
  }

  if( !mCurrentSimpleTestName.isEmpty() )
  {
    // argument to a simple test
    mCurrentSimpleTestArguments.append( QVariant( string ) );
    return;
  }

  mGotError = true;
  pushError( i18n( "Unexpected string argument outside of a simple test or simple command" ) );
}

void SieveDecoder::onNumberArgument( unsigned long number, char quantifier )
{
  Q_UNUSED( quantifier );

  if( mGotError )
    return; // ignore everything

  if( !mCurrentSimpleCommandName.isEmpty() )
  {
    Q_ASSERT( mCurrentSimpleTestName.isEmpty() );
    // argument to a simple command
    mCurrentSimpleCommandArguments.append( QVariant( (qulonglong)number ) );
    return;
  }

  if( !mCurrentSimpleTestName.isEmpty() )
  {
    // argument to a simple test
    mCurrentSimpleTestArguments.append( QVariant( (qulonglong)number ) );
    return;
  }

  mGotError = true;
  pushError( i18n( "Unexpected number argument outside of a simple test or simple command" ) );
}

void SieveDecoder::onStringListArgumentStart()
{
  if( mGotError )
    return; // ignore everything

  if( !mCurrentStringList.isEmpty() )
  {
    mGotError = true;
    pushError( i18n( "Unexpected start of string list inside a string list" ) ); 
  }
}

void SieveDecoder::onStringListEntry( const QString & string, bool multiLine, const QString & embeddedHashComment )
{
  Q_UNUSED( multiLine );
  Q_UNUSED( embeddedHashComment );

  if( mGotError )
    return; // ignore everything

  mCurrentStringList.append( string );
}

void SieveDecoder::onStringListArgumentEnd()
{
  if( mGotError )
    return; // ignore everything

  if( !mCurrentSimpleCommandName.isEmpty() )
  {
    Q_ASSERT( mCurrentSimpleTestName.isEmpty() );
    // argument to a simple command
    mCurrentSimpleCommandArguments.append( QVariant( mCurrentStringList ) );
    mCurrentStringList.clear();
    return;
  }

  if( !mCurrentSimpleTestName.isEmpty() )
  {
    // argument to a simple test
    mCurrentSimpleTestArguments.append( QVariant( mCurrentStringList ) );
    mCurrentStringList.clear();
    return;
  }

  mGotError = true;
  pushError( i18n( "Unexpected string list argument outside of a simple test or simple command" ) );
}

void SieveDecoder::onTestListStart()
{
  if( mGotError )
    return; // ignore everything

  if( mCurrentComponent->isRule() )
  {
    // assume "anyof"
    Rule * rule = static_cast< Rule * >( mCurrentComponent );
    if ( rule->condition() )
    {
      mGotError = true; 
      pushError( i18n( "Unexpected start of test list after a previous test" ) );
      return;  
    }
    Condition::Or * condition = componentFactory()->createOrCondition( mCurrentComponent );
    rule->setCondition( condition );
    mCurrentComponent = condition;
    return;
  }

  if( mCurrentComponent->isCondition() )
  {
    // this is a parenthesis after either an allof or anyof
    switch( static_cast< Condition::Base *>( mCurrentComponent )->conditionType() )
    {
      case Condition::ConditionTypeAnd:
      case Condition::ConditionTypeOr:
      case Condition::ConditionTypeNot:
        // ok
        return;
      break;
      default:
        // unexpected
      break;
    }
  }

  mGotError = true;
  pushError( i18n( "Unexpected start of test list" ) );

}

void SieveDecoder::onTestListEnd()
{
  if( mGotError )
    return; // ignore everything

  if( mCurrentComponent->isCondition() )
  {
    // this is a parenthesis after either an allof or anyof
    switch( static_cast< Condition::Base *>( mCurrentComponent )->conditionType() )
    {
      case Condition::ConditionTypeAnd:
      case Condition::ConditionTypeOr:
      case Condition::ConditionTypeNot:
        // ok
        return;
      break;
      default:
        // unexpected
      break;
    }
  }

  mGotError = true;
  pushError( i18n( "Unexpected end of test list" ) );
}

Rule * SieveDecoder::createRule( Component * parentComponent )
{
  Rule * rule = componentFactory()->createRule( parentComponent );
  rule->setAllProperties( mCurrentRuleProperties );
  mCurrentRuleProperties.clear();
  return rule;
}

void SieveDecoder::onCommandDescriptorStart( const QString & identifier )
{
  if( mGotError )
    return; // ignore everything

  if ( !mCurrentSimpleCommandName.isEmpty() )
  {
    mGotError = true;
    pushError( i18n( "Unexpected start of command inside a simple command... how you did that ?" ) );
    return;
  }

  if ( !( mCurrentComponent->isProgram() || mCurrentComponent->isRule() ) )
  {
    // unrecognized command
    mGotError = true; 
    pushError( i18n( "Unexpected start of command '%1'", identifier ) );
    return;
  }

  Rule * rule;
  Action::RuleList * ruleList;

  bool isIf = QString::compare( identifier, QLatin1String( "if" ), Qt::CaseInsensitive ) == 0;
  bool isElse;
  if( !isIf )
  {
    isElse = (
        ( QString::compare( identifier, QLatin1String( "elsif" ), Qt::CaseInsensitive ) == 0 ) ||
        ( QString::compare( identifier, QLatin1String( "else" ), Qt::CaseInsensitive ) == 0 )
      );
  } else {
    isElse = false;
  }

  if( isIf || isElse )
  {
    mCurrentSimpleCommandName = QString(); // this is not a simple command

    // conditional rule start
    if( mCurrentComponent->isProgram() )
    {
      // a rule for the current rule list
      ruleList = static_cast< Action::RuleList * >( mCurrentComponent );

    } else {
      // Not inside a program... so inside a rule.

      Q_ASSERT( mCurrentComponent->isRule() );

      // look if the rule's last action is a RuleList, if it isn't then create one.

      rule = static_cast< Rule * >( mCurrentComponent );

      ruleList = 0;

      if( rule->actionList()->count() > 0 )
      {
        if( rule->actionList()->last()->isRuleList() )
          ruleList = static_cast< Action::RuleList * >( rule->actionList()->last() );
      }

      if( !ruleList )
      {
        // create the rule list and add it as action
        ruleList = componentFactory()->createRuleList( mCurrentComponent );
        rule->addAction( ruleList );
      }
    }

    Q_ASSERT( ruleList );

    if( isElse )
    {
      // There must have been at least one previous rule in the list

      rule = ruleList->ruleList()->last();
      if( !rule )
      {
        mGotError = true; 
        pushError( i18n( "Unexpected else/elseif without a previous if" ) );
        return;
      }

      // If the previous rule wasn't terminal we should add a stop command inside
      // otherwise processing will continue in the elseif/else after the if has been
      // matched... and it's not nice :D

      QList< Action::Base * > * actions = rule->actionList();
      Q_ASSERT( actions );

      bool isTerminal = false;
      foreach( Action::Base * action, *actions )
      {
        if( action->isTerminal() )
        {
          isTerminal = true;
          break;
        }
      }

      if ( !isTerminal )
      {
        // Not terminal by itself: add a trailing stop action
        rule->addAction( componentFactory()->createStopAction( ruleList ) );
      }
    }

    // create the rule and add it to the rule list we've just found/created
    rule = createRule( ruleList );
    ruleList->addRule( rule );
    mCurrentComponent = rule;

    return;
  }

  // The simple commands can't have nested blocks...

  // We delay the creation of simple commands to the onCommandDescriptorEnd() so we have all the arguments
  mCurrentSimpleCommandName = identifier;
  mCurrentSimpleCommandArguments.clear();
}

void SieveDecoder::onCommandDescriptorEnd()
{
  if( mGotError )
    return; // ignore everything

  if( !( mCurrentComponent->isProgram() || mCurrentComponent->isRule() ) )
  {
    mGotError = true; 
    pushError( i18n( "Unexpected end of command out of a rule or toplevel rule list" ) );
    return;
  }

  if( !mCurrentSimpleCommandName.isEmpty() )
  {
    // delayed simple command creation

    if ( mCurrentComponent->isProgram() )
    {
      // unconditional rule start with an action
      Action::RuleList * ruleList = static_cast< Action::RuleList * >( mCurrentComponent );
      Rule * rule = createRule( ruleList );
      ruleList->addRule( rule );

      mCurrentComponent = rule;
    }

    Q_ASSERT( mCurrentComponent->isRule() );

    Action::Base * action;
    if(
         ( QString::compare( mCurrentSimpleCommandName, QLatin1String("stop" ), Qt::CaseInsensitive ) == 0 ) ||
         ( QString::compare( mCurrentSimpleCommandName, QLatin1String("keep" ), Qt::CaseInsensitive ) == 0 )
      )
    {
      action = componentFactory()->createStopAction( mCurrentComponent );
      Q_ASSERT( action );

    } else {
      // a "command" action

      const CommandDescriptor * command = componentFactory()->findCommand( mCurrentSimpleCommandName );
      if( !command )
      {
        mGotError = true; 
        pushError( i18n( "Unrecognized action '%1'", mCurrentSimpleCommandName ) );
        return;
      }

      if( mCurrentSimpleCommandArguments.count() < command->parameters()->count() )
      {
        mGotError = true; 
        pushError( i18n( "Action '%1' required at least %2 arguments", mCurrentSimpleCommandName, command->parameters()->count() ) );
        return;
      }

      action = componentFactory()->createCommand( mCurrentComponent, command, mCurrentSimpleCommandArguments );
      if( !action )
      {
        mGotError = true; 
        pushError( i18n( "Could not create action '%1': %2", mCurrentSimpleCommandName, componentFactory()->lastError() ) );
        return;
      }
    }

    Q_ASSERT( action );

    // a plain action within the current rule
    static_cast< Rule * >( mCurrentComponent )->addAction( action );

    mCurrentSimpleCommandName = QString();

    // nothing more to do: remain within the same rule now
    return;
  }

  if( mCurrentComponent->isProgram() )
    return; // nothing to pop

  // so it's a rule: pop it
  mCurrentComponent = mCurrentComponent->parent();

  Q_ASSERT( mCurrentComponent ); // must exist
  Q_ASSERT( mCurrentComponent->isRuleList() );

  // pop again, unless we're in a program (which can't be popped)
  if( !mCurrentComponent->isProgram() )
    mCurrentComponent = mCurrentComponent->parent();

  Q_ASSERT( mCurrentComponent->isProgram() || mCurrentComponent->isRule() );
}

void SieveDecoder::onBlockStart()
{
  if( mGotError )
    return; // ignore everything

  if ( !mCurrentComponent->isRule() )
  {
    mGotError = true;
    pushError( i18n( "Unexpected start of block outside of a rule." ) );
    return;
  }

  if ( !mCurrentSimpleCommandName.isEmpty() )
  {
    mGotError = true;
    pushError( i18n( "Unexpected start of block after a simple command." ) );
    return;
  }
}

void SieveDecoder::onBlockEnd()
{
  if( mGotError )
    return; // ignore everything
}

void SieveDecoder::onLineFeed()
{
  mLineNumber++;
}

void SieveDecoder::onComment( const QString &comment )
{
  if( mGotError )
    return; // ignore everything

  // we encode stuff in comments... no other way
  QStringList lines = comment.split( QChar( '\n' ), QString::SkipEmptyParts );
  foreach ( QString line, lines )
  {
    QString trimmed = line.trimmed();
    bool programProperty;

    if( trimmed.startsWith( QLatin1String( "program::" ) ) )
      programProperty = true;
    else if( trimmed.startsWith( QLatin1String( "rule::" ) ) )
      programProperty = false;
    else
      continue; // nothing interesting here

    trimmed.remove( 0, programProperty ? 9 : 6 );

    int idx = trimmed.indexOf( QChar( '=' ) );
    if( idx < 1 )
      continue; // doh

    QString name = QUrl::fromPercentEncoding( trimmed.left( idx ).trimmed().toLatin1() );
    QString value = QUrl::fromPercentEncoding( trimmed.remove( 0, idx + 1 ).trimmed().toLatin1() );

    if( name.isEmpty() )
      continue; // shouldn't happen but well...

    if( programProperty )
    {
      Q_ASSERT( mProgram );
      mProgram->setProperty( name, value );
    } else {
      mCurrentRuleProperties.insert( name, value );
    }

  }
}

Program * SieveDecoder::run( const QByteArray &encodedFilter )
{
  mLineNumber = 1;

  errorStack().clearErrors();

  KSieve::Parser parser( encodedFilter.data(), encodedFilter.data() + encodedFilter.length() );

  Private::SieveReader sieveReader( this );

  parser.setScriptBuilder( &sieveReader );

  if( mProgram )
    delete mProgram;

  mProgram = componentFactory()->createProgram();
  Q_ASSERT( mProgram ); // component componentfactory shall never fail creating a Program

  mGotError = false;
  mCurrentComponent = mProgram;

  if( !parser.parse() )
  {
    // force an error
    if( !mGotError )
    {
      mGotError = true;
      errorStack().pushError( "Sieve Decoder", i18n( "Internal sieve library error" ) );
    } else {
      errorStack().pushError( "Sieve Parser", parser.error().asString() );
      errorStack().pushError( "Sieve Decoder", i18n( "Sieve parsing error" ) );
    }
  }

#ifdef DEBUG_SIEVE_DECODER
  qDebug( "\nDECODING SIEVE SCRIPT\n" );
  qDebug( "%s", encodedFilter.data() );
  qDebug( "\nDECODED TREE\n" );
  if ( mProgram )
  {
    QString prefix;
    mProgram->dump( prefix );
  }
  qDebug( "\n" );

  if( mGotError )
    errorStack().dumpErrorMessage( "FILTER DECODING ERROR" );
#endif //DEBUG_SIEVE_DECODER

  if( mGotError )
    return 0;

  Program * prog = mProgram;
  mProgram = 0;
  return prog;
}


} // namespace IO

} // namespace Filter

} // namespace Akonadi

