/* === This file is part of Calamares - <https://github.com/calamares> ===
 *
 *   Copyright 2014, Teo Mrnjavac <teo@kde.org>
 *
 *   Calamares is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Calamares is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Calamares. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Retranslator.h"

#include "Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QTranslator>

/** @brief Helper class for loading translations
 *
 * This is used by the loadSingletonTranslator() function to hand off
 * work to translation-type specific code.
 */
struct TranslationLoader
{
    static QString mungeLocaleName( const QLocale& locale )
    {
        QString localeName = locale.name();
        localeName.replace( "-", "_" );

        if ( localeName == "C" )
        {
            localeName = "en";
        }

        // Special case of sr@latin
        //
        // See top-level CMakeLists.txt about special cases for translation loading.
        if ( locale.language() == QLocale::Language::Serbian && locale.script() == QLocale::Script::LatinScript )
        {
            localeName = QStringLiteral( "sr@latin" );
        }
        return localeName;
    }

    TranslationLoader( const QLocale& locale )
        : m_locale( locale )
        , m_localeName( mungeLocaleName( locale ) )
    {
    }

    virtual ~TranslationLoader() {};
    /// @brief Loads @p translator with the specific translations of this type
    virtual bool tryLoad( QTranslator* translator ) = 0;

    const QLocale& m_locale;
    QString m_localeName;
};

struct BrandingLoader : public TranslationLoader
{
    BrandingLoader( const QLocale& locale, const QString& prefix )
        : TranslationLoader( locale )
        , m_prefix( prefix )
    {
    }

    bool tryLoad( QTranslator* translator ) override
    {
        if ( m_prefix.isEmpty() )
        {
            return false;
        }
        QString brandingTranslationsDirPath( m_prefix );
        brandingTranslationsDirPath.truncate( m_prefix.lastIndexOf( QDir::separator() ) );
        QDir brandingTranslationsDir( brandingTranslationsDirPath );
        if ( brandingTranslationsDir.exists() )
        {
            QString filenameBase( m_prefix );
            filenameBase.remove( 0, m_prefix.lastIndexOf( QDir::separator() ) + 1 );
            if ( translator->load( m_locale, filenameBase, "_", brandingTranslationsDir.absolutePath() ) )
            {
                cDebug() << Logger::SubEntry << "Branding using locale:" << m_localeName;
                return true;
            }
            else
            {
                cDebug() << Logger::SubEntry << "Branding using default, system locale not found:" << m_localeName;
                // TODO: this loads something completely different
                return translator->load( m_prefix + "en" );
            }
        }
        return false;
    }

    QString m_prefix;
};

struct CalamaresLoader : public TranslationLoader
{
    using TranslationLoader::TranslationLoader;

    bool tryLoad( QTranslator* translator ) override
    {
        if ( translator->load( QString( ":/lang/calamares_" ) + m_localeName ) )
        {
            cDebug() << Logger::SubEntry << "Calamares using locale:" << m_localeName;
            return true;
        }
        else
        {
            cDebug() << Logger::SubEntry << "Calamares using default, system locale not found:" << m_localeName;
            return translator->load( QString( ":/lang/calamares_en" ) );
        }
    }
};

struct TZLoader : public TranslationLoader
{
    using TranslationLoader::TranslationLoader;
    bool tryLoad( QTranslator* translator ) override
    {
        if ( translator->load( QString( ":/lang/tz_" ) + m_localeName ) )
        {
            cDebug() << Logger::SubEntry << "Calamares Timezones using locale:" << m_localeName;
            return true;
        }
        else
        {
            cDebug() << Logger::SubEntry
                     << "Calamares Timezones using default, system locale not found:" << m_localeName;
            return translator->load( QString( ":/lang/tz_en" ) );
        }
    }
};

static void
loadSingletonTranslator( TranslationLoader&& loader, QTranslator*& translator_p )
{
    QTranslator* translator = new QTranslator();
    loader.tryLoad( translator );

    if ( translator_p )
    {
        QCoreApplication::removeTranslator( translator_p );
        delete translator_p;
    }

    QCoreApplication::installTranslator( translator );
    translator_p = translator;
}

namespace CalamaresUtils
{
static QTranslator* s_brandingTranslator = nullptr;
static QTranslator* s_translator = nullptr;
static QTranslator* s_tztranslator = nullptr;
static QString s_translatorLocaleName;

void
installTranslator( const QLocale& locale, const QString& brandingTranslationsPrefix, QObject* parent )
{
    loadSingletonTranslator( BrandingLoader( locale, brandingTranslationsPrefix ), s_brandingTranslator );
    loadSingletonTranslator( TZLoader( locale ), s_tztranslator );

    CalamaresLoader l( locale );  // because we want the extracted localeName
    loadSingletonTranslator( std::move( l ), s_translator );
    s_translatorLocaleName = l.m_localeName;
}


QString
translatorLocaleName()
{
    return s_translatorLocaleName;
}

Retranslator*
Retranslator::retranslatorFor( QObject* parent )
{
    Retranslator* r = nullptr;
    for ( QObject* child : parent->children() )
    {
        r = qobject_cast< Retranslator* >( child );
        if ( r )
        {
            return r;
        }
    }

    return new Retranslator( parent );
}

void
Retranslator::attachRetranslator( QObject* parent, std::function< void( void ) > retranslateFunc )
{
    retranslatorFor( parent )->m_retranslateFuncList.append( retranslateFunc );
    retranslateFunc();
}


Retranslator::Retranslator( QObject* parent )
    : QObject( parent )
{
    parent->installEventFilter( this );
}


bool
Retranslator::eventFilter( QObject* obj, QEvent* e )
{
    if ( obj == parent() )
    {
        if ( e->type() == QEvent::LanguageChange )
        {
            foreach ( std::function< void() > func, m_retranslateFuncList )
            {
                func();
            }
            emit languageChange();
        }
    }
    // pass the event on to the base
    return QObject::eventFilter( obj, e );
}


}  // namespace CalamaresUtils
