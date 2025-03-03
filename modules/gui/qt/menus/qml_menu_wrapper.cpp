/*****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#include "qml_menu_wrapper.hpp"
#include "menus.hpp"
#include "medialibrary/medialib.hpp"
#include "medialibrary/mlvideomodel.hpp"
#include "medialibrary/mlvideogroupsmodel.hpp"
#include "medialibrary/mlvideofoldersmodel.hpp"
#include "medialibrary/mlplaylistlistmodel.hpp"
#include "medialibrary/mlplaylistmodel.hpp"
#include "medialibrary/mlalbummodel.hpp"
#include "medialibrary/mlartistmodel.hpp"
#include "medialibrary/mlgenremodel.hpp"
#include "medialibrary/mlalbumtrackmodel.hpp"
#include "medialibrary/mlurlmodel.hpp"
#include "medialibrary/mlbookmarkmodel.hpp"
#include "network/networkdevicemodel.hpp"
#include "network/networkmediamodel.hpp"
#include "playlist/playlist_controller.hpp"
#include "playlist/playlist_model.hpp"
#include "dialogs/dialogs_provider.hpp"

#include <QSignalMapper>

namespace
{
    QIcon sortIcon(QWidget *widget, int order)
    {
        assert(order == Qt::AscendingOrder || order == Qt::DescendingOrder);

        QStyleOptionHeader headerOption;
        headerOption.init(widget);
        headerOption.sortIndicator = (order == Qt::AscendingOrder)
                ? QStyleOptionHeader::SortDown
                : QStyleOptionHeader::SortUp;

        QStyle *style = qApp->style();
        int arrowsize = style->pixelMetric(QStyle::PM_HeaderMarkSize, &headerOption, widget);
        if (arrowsize <= 0)
            arrowsize = 32;

        headerOption.rect = QRect(0, 0, arrowsize, arrowsize);
        QPixmap arrow(arrowsize, arrowsize);
        arrow.fill(Qt::transparent);

        {
            QPainter arrowPainter(&arrow);
            style->drawPrimitive(QStyle::PE_IndicatorHeaderArrow, &headerOption, &arrowPainter, widget);
        }

        return QIcon(arrow);
    }
}

void StringListMenu::popup(const QPoint &point, const QVariantList &stringList)
{
    QMenu *m = new QMenu;
    m->setAttribute(Qt::WA_DeleteOnClose);

    for (int i = 0; i != stringList.size(); ++i)
    {
        const auto str = stringList[i].toString();
        m->addAction(str, this, [this, i, str]()
        {
            emit selected(i, str);
        });
    }

    m->popup(point);
}

// SortMenu

SortMenu::~SortMenu()
{
    if (m_menu)
        delete m_menu;
}

// Functions

void SortMenu::popup(const QPoint &point, const bool popupAbovePoint, const QVariantList &model)
{
    if (m_menu)
        delete m_menu;

    m_menu = new QMenu;

    // model => [{text: "", checked: <bool>, order: <sort order> if checked else <invalid>}...]
    for (int i = 0; i != model.size(); ++i)
    {
        const auto obj = model[i].toMap();

        auto action = m_menu->addAction(obj.value("text").toString());
        action->setCheckable(true);

        const bool checked = obj.value("checked").toBool();
        action->setChecked(checked);

        if (checked)
            action->setIcon(sortIcon(m_menu, obj.value("order").toInt()));

        connect(action, &QAction::triggered, this, [this, i]()
        {
            emit selected(i);
        });
    }

    onPopup(m_menu);

    // m_menu->height() returns invalid height until initial popup call
    // so in case of 'popupAbovePoint', first show the menu and then reposition it
    m_menu->popup(point);
    if (popupAbovePoint)
    {
        // use 'popup' instead of 'move' so that menu can reposition itself if it's parts are hidden
        m_menu->popup(QPoint(point.x(), point.y() - m_menu->height()));
    }
}

void SortMenu::close()
{
    if (m_menu)
        m_menu->close();
}

// Protected functions

/* virtual */ void SortMenu::onPopup(QMenu *) {}

// SortMenuVideo

// Protected SortMenu reimplementation

void SortMenuVideo::onPopup(QMenu * menu) /* override */
{
    if (!m_ctx)
        return;

    menu->addSeparator();

    struct
    {
        const char * title;

        MainCtx::Grouping grouping;
    }
    entries[] =
    {
        { N_("Do not group videos"), MainCtx::GROUPING_NONE },
        { N_("Group by name"), MainCtx::GROUPING_NAME },
        { N_("Group by folder"), MainCtx::GROUPING_FOLDER },
    };

    QActionGroup * group = new QActionGroup(this);

    int index = m_ctx->grouping();

    for (size_t i = 0; i < ARRAY_SIZE(entries); i++)
    {
        QAction * action = menu->addAction(qtr(entries[i].title));

        action->setCheckable(true);

        MainCtx::Grouping grouping = entries[i].grouping;

        connect(action, &QAction::triggered, this, [this, grouping]()
        {
            emit this->grouping(grouping);
        });

        group->addAction(action);

        if (index == grouping)
            action->setChecked(true);
    }
}

QmlGlobalMenu::QmlGlobalMenu(QObject *parent)
    : VLCMenuBar(parent)
{
}

QmlGlobalMenu::~QmlGlobalMenu()
{
    if (m_menu)
        delete m_menu;
}

void QmlGlobalMenu::popup(QPoint pos)
{
    if (!m_ctx)
        return;

    qt_intf_t* p_intf = m_ctx->getIntf();
    if (!p_intf)
        return;

    if (m_menu)
        delete m_menu;

    m_menu = new QMenu();
    QMenu* submenu;

    connect( m_menu, &QMenu::aboutToShow, this, &QmlGlobalMenu::aboutToShow );
    connect( m_menu, &QMenu::aboutToHide, this, &QmlGlobalMenu::aboutToHide );

    submenu = m_menu->addMenu(qtr( "&Media" ));
    FileMenu( p_intf, submenu );

    /* Dynamic menus, rebuilt before being showed */
    submenu = m_menu->addMenu(qtr( "P&layback" ));
    NavigMenu( p_intf, submenu );

    submenu = m_menu->addMenu(qtr( "&Audio" ));
    AudioMenu( p_intf, submenu );

    submenu = m_menu->addMenu(qtr( "&Video" ));
    VideoMenu( p_intf, submenu );

    submenu = m_menu->addMenu(qtr( "Subti&tle" ));
    SubtitleMenu( p_intf, submenu );

    submenu = m_menu->addMenu(qtr( "Tool&s" ));
    ToolsMenu( p_intf, submenu );

    /* View menu, a bit different */
    submenu = m_menu->addMenu(qtr( "V&iew" ));
    ViewMenu( p_intf, submenu );

    submenu = m_menu->addMenu(qtr( "&Help" ));
    HelpMenu(submenu);

    m_menu->popup(pos);
}

QmlMenuBarMenu::QmlMenuBarMenu(QmlMenuBar* menubar, QWidget* parent)
    : QMenu(parent)
    , m_menubar(menubar)
{}

QmlMenuBarMenu::~QmlMenuBarMenu()
{
}

void QmlMenuBarMenu::mouseMoveEvent(QMouseEvent* mouseEvent)
{
    QPoint globalPos =m_menubar-> m_menu->mapToGlobal(mouseEvent->pos());
    if (m_menubar->getmenubar()->contains(m_menubar->getmenubar()->mapFromGlobal(globalPos))
        && !m_menubar->m_button->contains(m_menubar->m_button->mapFromGlobal(globalPos)))
    {
        m_menubar->setopenMenuOnHover(true);
        close();
        return;
    }
    QMenu::mouseMoveEvent(mouseEvent);
}

void QmlMenuBarMenu::keyPressEvent(QKeyEvent * event)
{
    QMenu::keyPressEvent(event);
    if (!event->isAccepted()
        && (event->key() == Qt::Key_Left  || event->key() == Qt::Key_Right))
    {
        event->accept();
        emit m_menubar->navigateMenu(event->key() == Qt::Key_Left ? -1 : 1);
    }
}

void QmlMenuBarMenu::keyReleaseEvent(QKeyEvent * event)
{
    QMenu::keyReleaseEvent(event);
}

QmlMenuBar::QmlMenuBar(QObject *parent)
    : VLCMenuBar(parent)
{
}

QmlMenuBar::~QmlMenuBar()
{
    if (m_menu)
        delete m_menu;
}

void QmlMenuBar::popupMenuCommon( QQuickItem* button, std::function<void(QMenu*)> createMenuFunc)
{
    if (!m_ctx || !m_menubar || !button)
        return;

    qt_intf_t* p_intf = m_ctx->getIntf();
    if (!p_intf)
        return;

    if (m_menu)
        delete m_menu;

    m_menu = new QmlMenuBarMenu(this, nullptr);
    createMenuFunc(m_menu);
    m_button = button;
    m_openMenuOnHover = false;
    connect(m_menu, &QMenu::aboutToHide, this, &QmlMenuBar::onMenuClosed);
    QPointF position = button->mapToGlobal(QPoint(0, button->height()));
    m_menu->popup(position.toPoint());
}

void QmlMenuBar::popupMediaMenu( QQuickItem* button )
{
    popupMenuCommon(button, [this](QMenu* menu) {
        qt_intf_t* p_intf = m_ctx->getIntf();
        FileMenu( p_intf, menu );
    });
}

void QmlMenuBar::popupPlaybackMenu( QQuickItem* button )
{
    popupMenuCommon(button, [this](QMenu* menu) {
        NavigMenu( m_ctx->getIntf(), menu );
    });
}

void QmlMenuBar::popupAudioMenu(QQuickItem* button )
{
    popupMenuCommon(button, [this](QMenu* menu) {
        AudioMenu( m_ctx->getIntf(), menu );
    });
}

void QmlMenuBar::popupVideoMenu( QQuickItem* button )
{
    popupMenuCommon(button, [this](QMenu* menu) {
        VideoMenu( m_ctx->getIntf(), menu );
    });
}

void QmlMenuBar::popupSubtitleMenu( QQuickItem* button )
{
    popupMenuCommon(button, [this](QMenu* menu) {
        SubtitleMenu( m_ctx->getIntf(), menu );
    });
}


void QmlMenuBar::popupToolsMenu( QQuickItem* button )
{
    popupMenuCommon(button, [this](QMenu* menu) {
        ToolsMenu( m_ctx->getIntf(), menu );
    });
}

void QmlMenuBar::popupViewMenu( QQuickItem* button )
{
    popupMenuCommon(button, [this](QMenu* menu) {
        qt_intf_t* p_intf = m_ctx->getIntf();
        ViewMenu( p_intf, menu );
    });
}

void QmlMenuBar::popupHelpMenu( QQuickItem* button )
{
    popupMenuCommon(button, [](QMenu* menu) {
        HelpMenu(menu);
    });
}

void QmlMenuBar::onMenuClosed()
{
    if (!m_openMenuOnHover)
        emit menuClosed();
}

// QmlBookmarkMenu

/* explicit */ QmlBookmarkMenu::QmlBookmarkMenu(QObject * parent) : QObject(parent) {}

QmlBookmarkMenu::~QmlBookmarkMenu()
{
    if (m_menu)
        delete m_menu;
}

// Interface

/* Q_INVOKABLE */ void QmlBookmarkMenu::popup(QPoint pos)
{
    if (m_ctx == nullptr || m_player == nullptr)
        return;

    if (m_menu)
        delete m_menu;

    m_menu = new QMenu;

    connect(m_menu, &QMenu::aboutToHide, this, &QmlBookmarkMenu::aboutToHide);
    connect(m_menu, &QMenu::aboutToShow, this, &QmlBookmarkMenu::aboutToShow);

    QAction * sectionTitles    = m_menu->addSection(qtr("Titles"));
    QAction * sectionChapters  = m_menu->addSection(qtr("Chapters"));
    QAction * sectionBookmarks = m_menu->addSection(qtr("Bookmarks"));

    // Titles

    TitleListModel * titles = m_player->getTitles();

    sectionTitles->setVisible(titles->rowCount() != 0);

    ListMenuHelper * helper = new ListMenuHelper(m_menu, titles, sectionChapters, m_menu);

    connect(helper, &ListMenuHelper::select, [titles](int index)
    {
        titles->setData(titles->index(index), true, Qt::CheckStateRole);
    });

    connect(helper, &ListMenuHelper::countChanged, [sectionTitles](int count)
    {
        // NOTE: The section should only be visible when the model has content.
        sectionTitles->setVisible(count != 0);
    });

    // Chapters

    ChapterListModel * chapters = m_player->getChapters();

    sectionChapters->setVisible(chapters->rowCount() != 0);

    helper = new ListMenuHelper(m_menu, chapters, sectionBookmarks, m_menu);

    connect(helper, &ListMenuHelper::select, [chapters](int index)
    {
        chapters->setData(chapters->index(index), true, Qt::CheckStateRole);
    });

    connect(helper, &ListMenuHelper::countChanged, [sectionChapters](int count)
    {
        // NOTE: The section should only be visible when the model has content.
        sectionChapters->setVisible(count != 0);
    });

    // Bookmarks

    // FIXME: Do we really need a translation call for the string shortcut ?
    m_menu->addAction(qtr("&Manage"), THEDP, &DialogsProvider::bookmarksDialog, qtr("Ctrl+B"));

    m_menu->addSeparator();

    MLBookmarkModel * bookmarks = new MLBookmarkModel(m_ctx->getMediaLibrary(),
                                                      m_player->getPlayer(), m_menu);

    helper = new ListMenuHelper(m_menu, bookmarks, nullptr, m_menu);

    connect(helper, &ListMenuHelper::select, [bookmarks](int index)
    {
        bookmarks->select(bookmarks->index(index, 0));
    });

    m_menu->popup(pos);
}

// QmlRendererMenu

/* explicit */ QmlRendererMenu::QmlRendererMenu(QObject * parent) : QObject(parent) {}

QmlRendererMenu::~QmlRendererMenu()
{
    if (m_menu)
        delete m_menu;
}

// Interface

/* Q_INVOKABLE */ void QmlRendererMenu::popup(QPoint pos)
{
    if (m_ctx == nullptr)
        return;

    if (m_menu)
        delete m_menu;

    m_menu = new RendererMenu(nullptr, m_ctx->getIntf());

    connect(m_menu, &QMenu::aboutToHide, this, &QmlRendererMenu::aboutToHide);
    connect(m_menu, &QMenu::aboutToShow, this, &QmlRendererMenu::aboutToShow);

    m_menu->popup(pos);
}

BaseMedialibMenu::BaseMedialibMenu(QObject* parent)
    : QObject(parent)
{}

BaseMedialibMenu::~BaseMedialibMenu()
{
    if (m_menu)
        delete m_menu;
}

void BaseMedialibMenu::medialibAudioContextMenu(MediaLib* ml, const QVariantList& mlId, const QPoint& pos, const QVariantMap& options)
{
    if (m_menu)
        delete m_menu;

    m_menu = new QMenu();
    QAction* action;

    action = m_menu->addAction( qtr("Add and play") );
    connect(action, &QAction::triggered, [ml, mlId]( ) {
        ml->addAndPlay(mlId);
    });

    action = m_menu->addAction( qtr("Enqueue") );
    connect(action, &QAction::triggered, [ml, mlId]( ) {
        ml->addToPlaylist(mlId);
    });

    action = m_menu->addAction( qtr("Add to playlist") );
    connect(action, &QAction::triggered, [mlId]( ) {
        DialogsProvider::getInstance()->playlistsDialog(mlId);
    });

    if (options.contains("information") && options["information"].type() == QVariant::Int) {

        action = m_menu->addAction( qtr("Information") );
        QSignalMapper* sigmapper = new QSignalMapper(m_menu);
        connect(action, &QAction::triggered, sigmapper, QOverload<>::of(&QSignalMapper::map));
        sigmapper->setMapping(action, options["information"].toInt());
        connect(sigmapper, QSIGNALMAPPER_MAPPEDINT_SIGNAL,
                this, &BaseMedialibMenu::showMediaInformation);
    }
    m_menu->popup(pos);
}

AlbumContextMenu::AlbumContextMenu(QObject* parent)
    : BaseMedialibMenu(parent)
{}

void AlbumContextMenu::popup(const QModelIndexList& selected, QPoint pos, QVariantMap options)
{
    BaseMedialibMenu::popup(m_model, MLAlbumModel::ALBUM_ID, selected, pos, options);
}


ArtistContextMenu::ArtistContextMenu(QObject* parent)
    : BaseMedialibMenu(parent)
{}

void ArtistContextMenu::popup(const QModelIndexList &selected, QPoint pos, QVariantMap options)
{
    BaseMedialibMenu::popup(m_model, MLArtistModel::ARTIST_ID, selected, pos, options);
}

GenreContextMenu::GenreContextMenu(QObject* parent)
    : BaseMedialibMenu(parent)
{}

void GenreContextMenu::popup(const QModelIndexList& selected, QPoint pos, QVariantMap options)
{
    BaseMedialibMenu::popup(m_model, MLGenreModel::GENRE_ID, selected, pos, options);
}

AlbumTrackContextMenu::AlbumTrackContextMenu(QObject* parent)
    : BaseMedialibMenu(parent)
{}

void AlbumTrackContextMenu::popup(const QModelIndexList &selected, QPoint pos, QVariantMap options)
{
    BaseMedialibMenu::popup(m_model, MLAlbumTrackModel::TRACK_ID, selected, pos, options);
}

URLContextMenu::URLContextMenu(QObject* parent)
    : BaseMedialibMenu(parent)
{}

void URLContextMenu::popup(const QModelIndexList &selected, QPoint pos, QVariantMap options)
{
    BaseMedialibMenu::popup(m_model, MLUrlModel::URL_ID, selected, pos, options);
}


VideoContextMenu::VideoContextMenu(QObject* parent)
    : QObject(parent)
{}

VideoContextMenu::~VideoContextMenu()
{
    if (m_menu)
        delete m_menu;
}

void VideoContextMenu::popup(const QModelIndexList& selected, QPoint pos, QVariantMap options)
{
    if (!m_model)
        return;

    if (m_menu)
        delete m_menu;

    m_menu = new QMenu();
    QAction* action;

    MediaLib* ml= m_model->ml();

    QVariantList itemIdList;
    for (const QModelIndex& modelIndex : selected)
        itemIdList.push_back(m_model->data(modelIndex, MLVideoModel::VIDEO_ID));

    action = m_menu->addAction( qtr("Add and play") );

    connect(action, &QAction::triggered, [ml, itemIdList, options]( ) {
        ml->addAndPlay(itemIdList, options["player-options"].toStringList());
    });

    action = m_menu->addAction( qtr("Enqueue") );
    connect(action, &QAction::triggered, [ml, itemIdList]( ) {
        ml->addToPlaylist(itemIdList);
    });

    action = m_menu->addAction( qtr("Add to playlist") );
    connect(action, &QAction::triggered, [itemIdList]( ) {
        DialogsProvider::getInstance()->playlistsDialog(itemIdList);
    });

    action = m_menu->addAction( qtr("Play as audio") );
    connect(action, &QAction::triggered, [ml, itemIdList, options]( ) {
        QStringList list = options["player-options"].toStringList();
        list.prepend(":no-video");
        ml->addAndPlay(itemIdList, list);
    });

    if (options.contains("information") && options["information"].type() == QVariant::Int) {
        action = m_menu->addAction( qtr("Information") );
        QSignalMapper* sigmapper = new QSignalMapper(m_menu);
        connect(action, &QAction::triggered, sigmapper, QOverload<>::of(&QSignalMapper::map));
        sigmapper->setMapping(action, options["information"].toInt());
        connect(sigmapper, QSIGNALMAPPER_MAPPEDINT_SIGNAL,
                this, &VideoContextMenu::showMediaInformation);
    }

    m_menu->popup(pos);
}

//=================================================================================================
// VideoGroupsContextMenu
//=================================================================================================

VideoGroupsContextMenu::VideoGroupsContextMenu(QObject * parent) : QObject(parent) {}

VideoGroupsContextMenu::~VideoGroupsContextMenu() /* override */
{
    if (m_menu)
        delete m_menu;
}

void VideoGroupsContextMenu::popup(const QModelIndexList & selected, QPoint pos,
                                   QVariantMap options)
{
    if (m_model == nullptr)
        return;

    if (m_menu)
        delete m_menu;

    QVariantList ids;

    for (const QModelIndex & index : selected)
        ids.push_back(m_model->data(index, MLVideoModel::VIDEO_ID));

    m_menu = new QMenu();

    MediaLib * ml = m_model->ml();

    QAction * action = m_menu->addAction(qtr("Add and play"));

    connect(action, &QAction::triggered, [ml, ids, options]()
    {
        ml->addAndPlay(ids, options["player-options"].toStringList());
    });

    action = m_menu->addAction(qtr("Enqueue"));

    connect(action, &QAction::triggered, [ml, ids]()
    {
        ml->addToPlaylist(ids);
    });

    action = m_menu->addAction(qtr("Add to playlist"));

    connect(action, &QAction::triggered, [ids]()
    {
        DialogsProvider::getInstance()->playlistsDialog(ids);
    });

    action = m_menu->addAction(qtr("Play as audio"));

    connect(action, &QAction::triggered, [ml, ids, options]()
    {
        QStringList list = options["player-options"].toStringList();

        list.prepend(":no-video");

        ml->addAndPlay(ids, list);
    });

    // NOTE: At the moment informations are only available for single video(s).
    if (selected.count() == 1
        &&
        m_model->data(selected.first(), MLVideoGroupsModel::GROUP_IS_VIDEO) == true
        &&
        options.contains("information") && options["information"].type() == QVariant::Int)
    {
        action = m_menu->addAction(qtr("Information"));

        QSignalMapper * mapper = new QSignalMapper(m_menu);

        mapper->setMapping(action, options["information"].toInt());

        connect(action, &QAction::triggered, mapper, QOverload<>::of(&QSignalMapper::map));

        connect(mapper, QSIGNALMAPPER_MAPPEDINT_SIGNAL,
                this, &VideoGroupsContextMenu::showMediaInformation);
    }

    m_menu->popup(pos);
}

// VideoFoldersContextMenu

VideoFoldersContextMenu::VideoFoldersContextMenu(QObject * parent) : QObject(parent) {}

VideoFoldersContextMenu::~VideoFoldersContextMenu() /* override */
{
    if (m_menu)
        delete m_menu;
}

void VideoFoldersContextMenu::popup(const QModelIndexList & selected, QPoint pos,
                                    QVariantMap options)
{
    if (m_model == nullptr)
        return;

    if (m_menu)
        delete m_menu;

    QVariantList ids;

    for (const QModelIndex & index : selected)
        ids.push_back(m_model->data(index, MLVideoFoldersModel::FOLDER_ID));

    m_menu = new QMenu();

    MediaLib * ml = m_model->ml();

    QAction * action = m_menu->addAction(qtr("Add and play"));

    connect(action, &QAction::triggered, [ml, ids, options]()
    {
        ml->addAndPlay(ids, options["player-options"].toStringList());
    });

    action = m_menu->addAction(qtr("Enqueue"));

    connect(action, &QAction::triggered, [ml, ids]()
    {
        ml->addToPlaylist(ids);
    });

    action = m_menu->addAction(qtr("Add to playlist"));

    connect(action, &QAction::triggered, [ids]()
    {
        DialogsProvider::getInstance()->playlistsDialog(ids);
    });

    action = m_menu->addAction(qtr("Play as audio"));

    connect(action, &QAction::triggered, [ml, ids, options]()
    {
        QStringList list = options["player-options"].toStringList();

        list.prepend(":no-video");

        ml->addAndPlay(ids, list);
    });

    m_menu->popup(pos);
}

//=================================================================================================
// PlaylistListContextMenu
//=================================================================================================

PlaylistListContextMenu::PlaylistListContextMenu(QObject * parent)
    : QObject(parent)
{}

PlaylistListContextMenu::~PlaylistListContextMenu() /* override */
{
    if (m_menu)
        delete m_menu;
}

void PlaylistListContextMenu::popup(const QModelIndexList & selected, QPoint pos, QVariantMap)
{
    if (!m_model)
        return;

    if (m_menu)
        delete m_menu;

    QVariantList ids;

    for (const QModelIndex & modelIndex : selected)
        ids.push_back(m_model->data(modelIndex, MLPlaylistListModel::PLAYLIST_ID));

    m_menu = new QMenu();

    MediaLib * ml = m_model->ml();

    QAction * action = m_menu->addAction(qtr("Add and play"));

    connect(action, &QAction::triggered, [ml, ids]() {
        ml->addAndPlay(ids);
    });

    action = m_menu->addAction(qtr("Enqueue"));

    connect(action, &QAction::triggered, [ml, ids]() {
        ml->addToPlaylist(ids);
    });

    action = m_menu->addAction(qtr("Delete"));

    connect(action, &QAction::triggered, [this, ids]() {
        m_model->deletePlaylists(ids);
    });

    m_menu->popup(pos);
}

//=================================================================================================
// PlaylistMediaContextMenu
//=================================================================================================

PlaylistMediaContextMenu::PlaylistMediaContextMenu(QObject * parent) : QObject(parent) {}

PlaylistMediaContextMenu::~PlaylistMediaContextMenu() /* override */
{
    if (m_menu)
        delete m_menu;
}

void PlaylistMediaContextMenu::popup(const QModelIndexList & selected, QPoint pos,
                                     QVariantMap options)
{
    if (!m_model)
        return;

    if (m_menu)
        delete m_menu;

    QVariantList ids;

    for (const QModelIndex& modelIndex : selected)
        ids.push_back(m_model->data(modelIndex, MLPlaylistModel::MEDIA_ID));

    m_menu = new QMenu();

    MediaLib * ml = m_model->ml();

    QAction * action = m_menu->addAction(qtr("Add and play"));

    connect(action, &QAction::triggered, [ml, ids]() {
        ml->addAndPlay(ids);
    });

    action = m_menu->addAction(qtr("Enqueue"));

    connect(action, &QAction::triggered, [ml, ids]() {
        ml->addToPlaylist(ids);
    });

    action = m_menu->addAction(qtr("Add to playlist"));

    connect(action, &QAction::triggered, [ids]() {
        DialogsProvider::getInstance()->playlistsDialog(ids);
    });

    action = m_menu->addAction(qtr("Play as audio"));

    connect(action, &QAction::triggered, [ml, ids]() {
        ml->addAndPlay(ids, {":no-video"});
    });

    if (options.contains("information") && options["information"].type() == QVariant::Int) {
        action = m_menu->addAction(qtr("Information"));

        QSignalMapper * mapper = new QSignalMapper(m_menu);

        connect(action, &QAction::triggered, mapper, QOverload<>::of(&QSignalMapper::map));

        mapper->setMapping(action, options["information"].toInt());
        connect(mapper, QSIGNALMAPPER_MAPPEDINT_SIGNAL,
                this, &PlaylistMediaContextMenu::showMediaInformation);
    }

    m_menu->addSeparator();

    action = m_menu->addAction(qtr("Remove Selected"));

    action->setIcon(QIcon(":/buttons/playlist/playlist_remove.svg"));

    connect(action, &QAction::triggered, [this, selected]() {
        m_model->remove(selected);
    });

    m_menu->popup(pos);
}

//=================================================================================================

NetworkMediaContextMenu::NetworkMediaContextMenu(QObject* parent)
    : QObject(parent)
{}

NetworkMediaContextMenu::~NetworkMediaContextMenu()
{
    if (m_menu)
        delete m_menu;
}

void NetworkMediaContextMenu::popup(const QModelIndexList& selected, QPoint pos)
{
    if (!m_model)
        return;

    if (m_menu)
        delete m_menu;

    m_menu = new QMenu();
    QAction* action;

    action = m_menu->addAction( qtr("Add and play") );
    connect(action, &QAction::triggered, [this, selected]( ) {
        m_model->addAndPlay(selected);
    });

    action = m_menu->addAction( qtr("Enqueue") );
    connect(action, &QAction::triggered, [this, selected]( ) {
        m_model->addToPlaylist(selected);
    });

    bool canBeIndexed = false;
    unsigned countIndexed = 0;
    for (const QModelIndex& idx : selected)
    {
        QVariant canIndex = m_model->data(m_model->index(idx.row()), NetworkMediaModel::NETWORK_CANINDEX );
        if (canIndex.isValid() && canIndex.toBool())
            canBeIndexed = true;
        else
            continue;
        QVariant isIndexed = m_model->data(m_model->index(idx.row()), NetworkMediaModel::NETWORK_INDEXED );
        if (!isIndexed.isValid())
            continue;
        if (isIndexed.toBool())
            ++countIndexed;
    }

    if (canBeIndexed)
    {
        bool removeFromML = countIndexed > 0;
        action = m_menu->addAction(removeFromML
            ? qtr("Remove from Media Library")
            : qtr("Add to Media Library"));

        connect(action, &QAction::triggered, [this, selected, removeFromML]( ) {
            for (const QModelIndex& idx : selected) {
                m_model->setData(m_model->index(idx.row()), !removeFromML, NetworkMediaModel::NETWORK_INDEXED);
            }
        });
    }

    m_menu->popup(pos);
}

NetworkDeviceContextMenu::NetworkDeviceContextMenu(QObject* parent)
    : QObject(parent)
{}

NetworkDeviceContextMenu::~NetworkDeviceContextMenu()
{
    if (m_menu)
        delete m_menu;
}

void NetworkDeviceContextMenu::popup(const QModelIndexList& selected, QPoint pos)
{
    if (!m_model)
        return;

    if (m_menu)
        delete m_menu;

    QMenu* menu = new QMenu();
    QAction* action;

    menu->setAttribute(Qt::WA_DeleteOnClose);

    action = menu->addAction( qtr("Add and play") );
    connect(action, &QAction::triggered, [this, selected]( ) {
        m_model->addAndPlay(selected);
    });

    action = menu->addAction( qtr("Enqueue") );
    connect(action, &QAction::triggered, [this, selected]( ) {
        m_model->addToPlaylist(selected);
    });

    menu->popup(pos);
}

PlaylistContextMenu::PlaylistContextMenu(QObject* parent)
    : QObject(parent)
{}

PlaylistContextMenu::~PlaylistContextMenu()
{
    if (m_menu)
        delete m_menu;
}
void PlaylistContextMenu::popup(int currentIndex, QPoint pos )
{
    if (!m_controler || !m_model)
        return;

    if (m_menu)
        delete m_menu;

    m_menu = new QMenu();
    QAction* action;

    QList<QUrl> selectedUrlList;
    for (const int modelIndex : m_model->getSelection())
        selectedUrlList.push_back(m_model->itemAt(modelIndex).getUrl());

    PlaylistItem currentItem;
    if (currentIndex >= 0)
        currentItem = m_model->itemAt(currentIndex);

    if (currentItem)
    {
        action = m_menu->addAction( qtr("Play") );
        connect(action, &QAction::triggered, [this, currentIndex]( ) {
            m_controler->goTo(currentIndex, true);
        });

        m_menu->addSeparator();
    }

    if (m_model->getSelectedCount() > 0) {
        action = m_menu->addAction( qtr("Stream") );
        connect(action, &QAction::triggered, [selectedUrlList]( ) {
            DialogsProvider::getInstance()->streamingDialog(selectedUrlList, false);
        });

        action = m_menu->addAction( qtr("Save") );
        connect(action, &QAction::triggered, [selectedUrlList]( ) {
            DialogsProvider::getInstance()->streamingDialog(selectedUrlList, true);
        });

        m_menu->addSeparator();
    }

    if (currentItem) {
        action = m_menu->addAction( qtr("Information") );
        action->setIcon(QIcon(":/menu/info.svg"));
        connect(action, &QAction::triggered, [currentItem]( ) {
            DialogsProvider::getInstance()->mediaInfoDialog(currentItem);
        });

        m_menu->addSeparator();

        action = m_menu->addAction( qtr("Show Containing Directory...") );
        action->setIcon(QIcon(":/type/folder-grey.svg"));
        connect(action, &QAction::triggered, [this, currentItem]( ) {
            m_controler->explore(currentItem);
        });

        m_menu->addSeparator();
    }

    action = m_menu->addAction( qtr("Add File...") );
    action->setIcon(QIcon(":/buttons/playlist/playlist_add.svg"));
    connect(action, &QAction::triggered, []( ) {
        DialogsProvider::getInstance()->simpleOpenDialog(false);
    });

    action = m_menu->addAction( qtr("Add Directory...") );
    action->setIcon(QIcon(":/buttons/playlist/playlist_add.svg"));
    connect(action, &QAction::triggered, []( ) {
        DialogsProvider::getInstance()->PLAppendDir();
    });

    action = m_menu->addAction( qtr("Advanced Open...") );
    action->setIcon(QIcon(":/buttons/playlist/playlist_add.svg"));
    connect(action, &QAction::triggered, []( ) {
        DialogsProvider::getInstance()->PLAppendDialog();
    });

    m_menu->addSeparator();

    if (m_model->getSelectedCount() > 0)
    {
        action = m_menu->addAction( qtr("Save Playlist to File...") );
        connect(action, &QAction::triggered, []( ) {
            DialogsProvider::getInstance()->savePlayingToPlaylist();
        });

        m_menu->addSeparator();

        action = m_menu->addAction( qtr("Remove Selected") );
        action->setIcon(QIcon(":/buttons/playlist/playlist_remove.svg"));
        connect(action, &QAction::triggered, [this]( ) {
            m_model->removeItems(m_model->getSelection());
        });
    }

    action = m_menu->addAction( qtr("Clear the playlist") );
    action->setIcon(QIcon(":/toolbar/clear.svg"));
    connect(action, &QAction::triggered, [this]( ) {
        m_controler->clear();
    });

    m_menu->popup(pos);
}
