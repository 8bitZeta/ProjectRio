// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/NetPlay/NetPlaySetupDialog.h"

#include <memory>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QGroupBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QRadioButton>
#include <QHeaderView>
#include <QInputDialog>

#include "Core/Config/NetplaySettings.h"
#include "Core/NetPlayProto.h"

#include "DolphinQt/QtUtils/ModalMessageBox.h"
#include "DolphinQt/QtUtils/NonDefaultQPushButton.h"
#include "DolphinQt/QtUtils/UTF8CodePointCountValidator.h"
#include "DolphinQt/Settings.h"

#include "UICommon/GameFile.h"
#include "UICommon/NetPlayIndex.h"
#include "DolphinQt/NetPlay/NetPlayBrowser.h"
#include "Common/Version.h"
//#include "Core/NetPlayServer.h"


NetPlaySetupDialog::NetPlaySetupDialog(const GameListModel& game_list_model, QWidget* parent)
    : QDialog(parent), m_game_list_model(game_list_model)
{
  setWindowTitle(tr("NetPlay Setup"));
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

  CreateMainLayout();

  bool use_index = Config::Get(Config::NETPLAY_USE_INDEX);
  std::string index_region = Config::Get(Config::NETPLAY_INDEX_REGION);
  std::string index_name = Config::LobbyNameVector(Config::Get(Config::NETPLAY_INDEX_NAME))[0];
  std::string index_password = Config::Get(Config::NETPLAY_INDEX_PASSWORD);
  std::string nickname = Config::Get(Config::NETPLAY_NICKNAME);
  std::string traversal_choice = Config::Get(Config::NETPLAY_TRAVERSAL_CHOICE);
  int connect_port = Config::Get(Config::NETPLAY_CONNECT_PORT);
  int host_port = Config::Get(Config::NETPLAY_HOST_PORT);
  int host_listen_port = Config::Get(Config::NETPLAY_LISTEN_PORT);
  bool enable_chunked_upload_limit = Config::Get(Config::NETPLAY_ENABLE_CHUNKED_UPLOAD_LIMIT);
  u32 chunked_upload_limit = Config::Get(Config::NETPLAY_CHUNKED_UPLOAD_LIMIT);
#ifdef USE_UPNP
  bool use_upnp = Config::Get(Config::NETPLAY_USE_UPNP);

  m_host_upnp->setChecked(use_upnp);
#endif

  m_nickname_edit->setText(QString::fromStdString(nickname));
  m_connection_type->setCurrentIndex(traversal_choice == "direct" ? 0 : 1);
  m_connect_port_box->setValue(connect_port);
  m_host_port_box->setValue(host_port);

  m_host_force_port_box->setValue(host_listen_port);
  m_host_force_port_box->setEnabled(false);

  m_host_server_browser->setChecked(use_index);

  m_host_server_region->setEnabled(use_index);
  m_host_server_region->setCurrentIndex(
      m_host_server_region->findData(QString::fromStdString(index_region)));

  m_host_server_name->setEnabled(use_index);
  m_host_server_name->setText(QString::fromStdString(index_name));

  bool is_ranked = Config::Get(Config::NETPLAY_RANKED);
  m_host_ranked->setChecked(is_ranked);
  m_host_game_mode->setEnabled(true);

  m_host_server_password->setEnabled(use_index);
  m_host_server_password->setText(QString::fromStdString(index_password));

  m_host_chunked_upload_limit_check->setChecked(enable_chunked_upload_limit);
  m_host_chunked_upload_limit_box->setValue(chunked_upload_limit);
  m_host_chunked_upload_limit_box->setEnabled(enable_chunked_upload_limit);

  // Browser Stuff
  const auto& settings = Settings::Instance().GetQSettings();

  const QByteArray geometry =
      settings.value(QStringLiteral("netplaybrowser/geometry")).toByteArray();
  if (!geometry.isEmpty())
    restoreGeometry(geometry);

  const QString region = settings.value(QStringLiteral("netplaybrowser/region")).toString();
  const bool valid_region = m_region_combo->findText(region) != -1;
  if (valid_region)
    m_region_combo->setCurrentText(region);

  m_edit_name->setText(settings.value(QStringLiteral("netplaybrowser/name")).toString());

  const QString visibility = settings.value(QStringLiteral("netplaybrowser/visibility")).toString();
  if (visibility == QStringLiteral("public"))
    m_radio_public->setChecked(true);
  else if (visibility == QStringLiteral("private"))
    m_radio_private->setChecked(true);

  m_check_hide_ingame->setChecked(true);


  OnConnectionTypeChanged(m_connection_type->currentIndex());

  ConnectWidgets();

  m_refresh_run.Set(true);
  m_refresh_thread = std::thread([this] { RefreshLoopBrowser(); });

  UpdateListBrowser();
  RefreshBrowser();
}

void NetPlaySetupDialog::CreateMainLayout()
{
  m_main_layout = new QGridLayout;
  m_button_box = new QDialogButtonBox(QDialogButtonBox::Cancel);
  m_nickname_edit = new QLineEdit;
  m_connection_type = new QComboBox;
  m_connection_type->setCurrentIndex(1); // default to traversal server
  m_reset_traversal_button = new NonDefaultQPushButton(tr("Reset Traversal Settings"));
  m_tab_widget = new QTabWidget;

  m_nickname_edit->setValidator(
      new UTF8CodePointCountValidator(NetPlay::MAX_NAME_LENGTH, m_nickname_edit));

  // Connection widget
  auto* connection_widget = new QWidget;
  auto* connection_layout = new QGridLayout;

  // NetPlay Browser
  auto* browser_widget = new QWidget;
  auto* layout = new QVBoxLayout;

  m_table_widget = new QTableWidget;
  m_table_widget->setTabKeyNavigation(false);

  m_table_widget->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table_widget->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table_widget->setWordWrap(false);

  m_region_combo = new QComboBox;

  m_region_combo->addItem(tr("Any Region"));

  for (const auto& region : NetPlayIndex::GetRegions())
  {
    m_region_combo->addItem(
        tr("%1 (%2)").arg(tr(region.second.c_str())).arg(QString::fromStdString(region.first)),
        QString::fromStdString(region.first));
  }

  m_region_combo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

  m_status_label = new QLabel;
  m_online_count = new QLabel;
  m_b_button_box = new QDialogButtonBox;
  m_button_refresh = new QPushButton(tr("Refresh"));
  m_edit_name = new QLineEdit;
  m_check_hide_ingame = new QCheckBox(tr("Hide In-Game Sessions"));

  m_radio_all = new QRadioButton(tr("Private and Public"));
  m_radio_private = new QRadioButton(tr("Private"));
  m_radio_public = new QRadioButton(tr("Public"));

  m_radio_all->setChecked(true);

  auto* filter_box = new QGroupBox(tr("Filters"));
  auto* filter_layout = new QGridLayout;
  filter_box->setLayout(filter_layout);

  filter_layout->addWidget(new QLabel(tr("Region:")), 0, 0);
  filter_layout->addWidget(m_region_combo, 0, 1, 1, -1);
  filter_layout->addWidget(new QLabel(tr("Lobby Name:")), 1, 0);
  filter_layout->addWidget(m_edit_name, 1, 1, 1, -1);
  filter_layout->addWidget(m_radio_all, 2, 1);
  filter_layout->addWidget(m_radio_public, 2, 2);
  filter_layout->addWidget(m_radio_private, 2, 3);
  filter_layout->addItem(new QSpacerItem(3, 1, QSizePolicy::Expanding), 3, 4);
  filter_layout->addWidget(m_check_hide_ingame, 4, 1, 1, -1);

  layout->addWidget(m_online_count);
  layout->addWidget(m_table_widget);
  layout->addWidget(filter_box);
  layout->addWidget(m_status_label);
  layout->addWidget(m_b_button_box);

  m_b_button_box->addButton(m_button_refresh, QDialogButtonBox::ResetRole);

  browser_widget->setLayout(layout);

  m_ip_label = new QLabel;
  m_ip_edit = new QLineEdit;
  m_connect_port_label = new QLabel(tr("Port:"));
  m_connect_port_box = new QSpinBox;
  m_connect_button = new NonDefaultQPushButton(tr("Connect"));

  m_connect_port_box->setMaximum(65535);

  connection_layout->addWidget(m_ip_label, 0, 0);
  connection_layout->addWidget(m_ip_edit, 0, 1);
  connection_layout->addWidget(m_connect_port_label, 0, 2);
  connection_layout->addWidget(m_connect_port_box, 0, 3);
  connection_layout->addWidget(
      new QLabel(
          tr("ALERT:\n\n"
             "All players must use the same Dolphin version.\n"
             "If enabled, SD cards must be identical between players.\n"
             "If DSP LLE is used, DSP ROMs must be identical between players.\n"
             "If a game is hanging on boot, it may not support Dual Core Netplay."
             " Disable Dual Core.\n"
             "If connecting directly, the host must have the chosen UDP port open/forwarded!\n"
             "\n"
             "Wii Remote support in netplay is experimental and may not work correctly.\n"
             "Use at your own risk.\n")),
      1, 0, -1, -1);
  connection_layout->addWidget(m_connect_button, 3, 3, Qt::AlignRight);

  connection_widget->setLayout(connection_layout);

  // Host widget
  auto* host_widget = new QWidget;
  auto* host_layout = new QGridLayout;
  m_host_port_label = new QLabel(tr("Port:"));
  m_host_port_box = new QSpinBox;
  m_host_force_port_check = new QCheckBox(tr("Force Listen Port:"));
  m_host_force_port_box = new QSpinBox;
  m_host_chunked_upload_limit_check = new QCheckBox(tr("Limit Chunked Upload Speed:"));
  m_host_chunked_upload_limit_box = new QSpinBox;
  m_host_server_browser = new QCheckBox(tr("Show in server browser"));
  m_host_server_name = new QLineEdit;
  m_host_server_password = new QLineEdit;
  m_host_server_region = new QComboBox;

  // m_host_option_label = new QLabel(tr("Game Options:"));
  m_host_option_label = new QLabel;
  m_host_option_label->setTextFormat(Qt::RichText);
  m_host_option_label->setText(tr("<b><u>Game Options</u>:</b>"));

  m_host_ranked = new QCheckBox(tr("Ranked Mode"));
  m_host_ranked->setToolTip(
      tr("Enabling Ranked Mode will mark down your games as being ranked in the stats files\n and "
         "disable any extra gecko codes as well as Training Mode. This should be toggled for\n"
         "serious/competitive/ranked games ase accurate and organized. Toggling this box will\n"
         " always record stats, ignoring user configurations."));
  m_host_game_mode = new QComboBox;
  m_host_game_mode->setToolTip(
      tr("Choose which game mode you would like to play with. This will appear and be visible to other players in the lobby browser.\n"
      "- Superstars OFF: doesn't allow superstarred characters to be used\n"
      "- Superstars ON: allows the use of superstarred characters\n"
      "- Custom: any non-standard format"
      ));

  // add game modes
  m_host_game_mode->addItem(tr("Superstars OFF"));
  m_host_game_mode->addItem(tr("Superstars ON"));
  m_host_game_mode->addItem(tr("Custom"));

  std::string current_mode = Config::Get(Config::NETPLAY_GAME_MODE);
  int iMode = m_host_game_mode->findText(QString::fromStdString(current_mode));
  m_host_game_mode->setCurrentIndex(iMode == -1 ? 0 : iMode);

#ifdef USE_UPNP
  m_host_upnp = new QCheckBox(tr("Forward port (UPnP)"));
#endif
  m_host_games = new QListWidget;
  m_host_button = new NonDefaultQPushButton(tr("Host"));

  m_host_port_box->setMaximum(65535);
  m_host_force_port_box->setMaximum(65535);
  m_host_chunked_upload_limit_box->setRange(1, 1000000);
  m_host_chunked_upload_limit_box->setSingleStep(100);
  m_host_chunked_upload_limit_box->setSuffix(QStringLiteral(" kbps"));

  m_host_chunked_upload_limit_check->setToolTip(tr(
      "This will limit the speed of chunked uploading per client, which is used for save sync."));

  m_host_server_name->setToolTip(tr("Name of your session shown in the server browser"));
  m_host_server_name->setPlaceholderText(tr("Lobby Name"));
  m_host_server_password->setToolTip(tr("Password for joining your game (leave empty for none)"));
  m_host_server_password->setPlaceholderText(tr("Password"));

  for (const auto& region : NetPlayIndex::GetRegions())
  {
    m_host_server_region->addItem(
        tr("%1 (%2)").arg(tr(region.second.c_str())).arg(QString::fromStdString(region.first)),
        QString::fromStdString(region.first));
  }
  QLabel* separator = new QLabel(tr(" "));
  host_layout->addWidget(m_host_port_label, 0, 0);
  host_layout->addWidget(m_host_port_box, 0, 1);
#ifdef USE_UPNP
  host_layout->addWidget(m_host_upnp, 0, 2);
#endif
  host_layout->addWidget(m_host_server_browser, 1, 0);
  host_layout->addWidget(m_host_server_region, 1, 1);
  host_layout->addWidget(m_host_server_name, 1, 2);
  host_layout->addWidget(m_host_server_password, 1, 3);
  host_layout->addWidget(separator, 2, 0);
  host_layout->addWidget(m_host_option_label, 3, 0);
  host_layout->addWidget(m_host_ranked, 4, 0);
  host_layout->addWidget(m_host_game_mode, 4, 1);
  host_layout->addWidget(m_host_games, 5, 0, 1, -1);
  host_layout->addWidget(m_host_force_port_check, 6, 0);
  host_layout->addWidget(m_host_force_port_box, 6, 1, Qt::AlignLeft);
  host_layout->addWidget(m_host_chunked_upload_limit_check, 7, 0);
  host_layout->addWidget(m_host_chunked_upload_limit_box, 7, 1, Qt::AlignLeft);
  host_layout->addWidget(m_host_button, 7, 3, 2, 1, Qt::AlignRight);

  host_widget->setLayout(host_layout);

  m_connection_type->addItem(tr("Direct Connection"));
  m_connection_type->addItem(tr("Traversal Server"));

  m_main_layout->addWidget(new QLabel(tr("Connection Type:")), 0, 0);
  m_main_layout->addWidget(m_connection_type, 0, 1);
  m_main_layout->addWidget(m_reset_traversal_button, 0, 2);
  m_main_layout->addWidget(new QLabel(tr("Nickname:")), 1, 0);
  m_main_layout->addWidget(m_nickname_edit, 1, 1);
  m_main_layout->addWidget(m_tab_widget, 2, 0, 1, -1);
  m_main_layout->addWidget(m_button_box, 3, 0, 1, -1);

  // Tabs
  m_tab_widget->addTab(connection_widget, tr("Join Private Lobby"));
  m_tab_widget->addTab(host_widget, tr("Host Lobby"));
  m_tab_widget->addTab(browser_widget, tr("Lobby Browser"));


  setLayout(m_main_layout);
}

NetPlaySetupDialog::~NetPlaySetupDialog()
{
  m_refresh_run.Set(false);
  m_refresh_event.Set();
  if (m_refresh_thread.joinable())
    m_refresh_thread.join();

  SaveSettings();
}

void NetPlaySetupDialog::ConnectWidgets()
{
  connect(m_connection_type, qOverload<int>(&QComboBox::currentIndexChanged), this,
          &NetPlaySetupDialog::OnConnectionTypeChanged);
  connect(m_nickname_edit, &QLineEdit::textChanged, this, &NetPlaySetupDialog::SaveSettings);

  // Connect widget
  connect(m_ip_edit, &QLineEdit::textChanged, this, &NetPlaySetupDialog::SaveSettings);
  connect(m_connect_port_box, qOverload<int>(&QSpinBox::valueChanged), this,
          &NetPlaySetupDialog::SaveSettings);
  // Host widget
  connect(m_host_port_box, qOverload<int>(&QSpinBox::valueChanged), this,
          &NetPlaySetupDialog::SaveSettings);
  connect(m_host_games, qOverload<int>(&QListWidget::currentRowChanged), [this](int index) {
    Settings::GetQSettings().setValue(QStringLiteral("netplay/hostgame"),
                                      m_host_games->item(index)->text());
  });

  // refresh browser on tab changed
  connect(m_tab_widget, &QTabWidget::currentChanged, this, &NetPlaySetupDialog::RefreshBrowser);

  connect(m_host_games, &QListWidget::itemDoubleClicked, this, &NetPlaySetupDialog::accept);

  connect(m_host_force_port_check, &QCheckBox::toggled,
          [this](bool value) { m_host_force_port_box->setEnabled(value); });
  connect(m_host_chunked_upload_limit_check, &QCheckBox::toggled, this, [this](bool value) {
    m_host_chunked_upload_limit_box->setEnabled(value);
    SaveSettings();
  });
  connect(m_host_chunked_upload_limit_box, qOverload<int>(&QSpinBox::valueChanged), this,
          &NetPlaySetupDialog::SaveSettings);

  connect(m_host_server_browser, &QCheckBox::toggled, this, &NetPlaySetupDialog::SaveSettings);
  connect(m_host_server_name, &QLineEdit::textChanged, this, &NetPlaySetupDialog::SaveSettings);
  connect(m_host_server_password, &QLineEdit::textChanged, this, &NetPlaySetupDialog::SaveSettings);
  connect(m_host_server_region,
          static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
          &NetPlaySetupDialog::SaveSettings);

#ifdef USE_UPNP
  connect(m_host_upnp, &QCheckBox::stateChanged, this, &NetPlaySetupDialog::SaveSettings);
#endif

  connect(m_connect_button, &QPushButton::clicked, this, &QDialog::accept);
  connect(m_host_button, &QPushButton::clicked, this, &QDialog::accept);
  connect(m_button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_reset_traversal_button, &QPushButton::clicked, this,
          &NetPlaySetupDialog::ResetTraversalHost);
  connect(m_host_server_browser, &QCheckBox::toggled, this, [this](bool value) {
    m_host_server_region->setEnabled(value);
    m_host_server_name->setEnabled(value);
    m_host_server_password->setEnabled(value);
  });

  // connect this to lobby data stuff
  connect(m_host_game_mode, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
          this, &NetPlaySetupDialog::SaveLobbySettings);
  connect(m_host_ranked, &QCheckBox::toggled, this, &NetPlaySetupDialog::SaveLobbySettings);

  // Browser Stuff
  connect(m_region_combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
          &NetPlaySetupDialog::RefreshBrowser);

  connect(m_button_refresh, &QPushButton::clicked, this, &NetPlaySetupDialog::RefreshBrowser);

  connect(m_radio_all, &QRadioButton::toggled, this, &NetPlaySetupDialog::RefreshBrowser);
  connect(m_radio_private, &QRadioButton::toggled, this, &NetPlaySetupDialog::RefreshBrowser);
  connect(m_check_hide_ingame, &QRadioButton::toggled, this, &NetPlaySetupDialog::RefreshBrowser);

  connect(m_edit_name, &QLineEdit::textChanged, this, &NetPlaySetupDialog::RefreshBrowser);

  connect(m_table_widget, &QTableWidget::itemDoubleClicked, this,
          &NetPlaySetupDialog::acceptBrowser);

  connect(this, &NetPlaySetupDialog::UpdateStatusRequestedBrowser, this,
          &NetPlaySetupDialog::OnUpdateStatusRequestedBrowser, Qt::QueuedConnection);
  connect(this, &NetPlaySetupDialog::UpdateListRequestedBrowser, this,
          &NetPlaySetupDialog::OnUpdateListRequestedBrowser,
          Qt::QueuedConnection);
}

void NetPlaySetupDialog::SaveLobbySettings()
{
  Config::SetBaseOrCurrent(Config::NETPLAY_RANKED, m_host_ranked->isChecked());
  Config::SetBaseOrCurrent(Config::NETPLAY_GAME_MODE,
                           m_host_game_mode->currentText().toStdString());
  Config::Save();
}

void NetPlaySetupDialog::SaveSettings()
{
  Config::ConfigChangeCallbackGuard config_guard;

  Config::SetBaseOrCurrent(Config::NETPLAY_NICKNAME, m_nickname_edit->text().toStdString());
  Config::SetBaseOrCurrent(m_connection_type->currentIndex() == 0 ? Config::NETPLAY_ADDRESS :
                                                                    Config::NETPLAY_HOST_CODE,
                           m_ip_edit->text().toStdString());
  Config::SetBaseOrCurrent(Config::NETPLAY_CONNECT_PORT,
                           static_cast<u16>(m_connect_port_box->value()));
  Config::SetBaseOrCurrent(Config::NETPLAY_HOST_PORT, static_cast<u16>(m_host_port_box->value()));
#ifdef USE_UPNP
  Config::SetBaseOrCurrent(Config::NETPLAY_USE_UPNP, m_host_upnp->isChecked());
#endif

  if (m_host_force_port_check->isChecked())
    Config::SetBaseOrCurrent(Config::NETPLAY_LISTEN_PORT,
                             static_cast<u16>(m_host_force_port_box->value()));

  Config::SetBaseOrCurrent(Config::NETPLAY_ENABLE_CHUNKED_UPLOAD_LIMIT,
                           m_host_chunked_upload_limit_check->isChecked());
  Config::SetBaseOrCurrent(Config::NETPLAY_CHUNKED_UPLOAD_LIMIT,
                           m_host_chunked_upload_limit_box->value());

  Config::SetBaseOrCurrent(Config::NETPLAY_USE_INDEX, m_host_server_browser->isChecked());
  Config::SetBaseOrCurrent(Config::NETPLAY_INDEX_REGION,
                           m_host_server_region->currentData().toString().toStdString());
  Config::SetBaseOrCurrent(Config::NETPLAY_INDEX_NAME, LobbyNameString());
  Config::SetBaseOrCurrent(Config::NETPLAY_INDEX_PASSWORD,
                           m_host_server_password->text().toStdString());

  // Browser Stuff
  auto& settings = Settings::Instance().GetQSettings();

  settings.setValue(QStringLiteral("netplaybrowser/geometry"), saveGeometry());
  settings.setValue(QStringLiteral("netplaybrowser/region"), m_region_combo->currentText());
  settings.setValue(QStringLiteral("netplaybrowser/name"), m_edit_name->text());

  QString visibility(QStringLiteral("all"));
  if (m_radio_public->isChecked())
    visibility = QStringLiteral("public");
  else if (m_radio_private->isChecked())
    visibility = QStringLiteral("private");
  settings.setValue(QStringLiteral("netplaybrowser/visibility"), visibility);

  settings.setValue(QStringLiteral("netplaybrowser/hide_incompatible"), true);
  settings.setValue(QStringLiteral("netplaybrowser/hide_ingame"), m_check_hide_ingame->isChecked());
}

void NetPlaySetupDialog::OnConnectionTypeChanged(int index)
{
  m_connect_port_box->setHidden(index != 0);
  m_connect_port_label->setHidden(index != 0);

  m_host_port_label->setHidden(index != 0);
  m_host_port_box->setHidden(index != 0);
#ifdef USE_UPNP
  m_host_upnp->setHidden(index != 0);
#endif
  m_host_force_port_check->setHidden(index == 0);
  m_host_force_port_box->setHidden(index == 0);

  m_reset_traversal_button->setHidden(index == 0);

  std::string address =
      index == 0 ? Config::Get(Config::NETPLAY_ADDRESS) : Config::Get(Config::NETPLAY_HOST_CODE);

  m_ip_label->setText(index == 0 ? tr("IP Address:") : tr("Host Code:"));
  m_ip_edit->setText(QString::fromStdString(address));

  Config::SetBaseOrCurrent(Config::NETPLAY_TRAVERSAL_CHOICE,
                           std::string(index == 0 ? "direct" : "traversal"));
}

void NetPlaySetupDialog::show()
{
  // Here i'm setting the lobby name if it's empty to make
  // NetPlay sessions start more easily for first time players
  if (m_host_server_name->text().isEmpty())
  {
    std::string nickname = Config::Get(Config::NETPLAY_NICKNAME);
    m_host_server_name->setText(QString::fromStdString(nickname));
  }
  m_host_server_browser->setChecked(true);
  m_connection_type->setCurrentIndex(1);
  m_tab_widget->setCurrentIndex(2); // start on browser
  RefreshBrowser();

  PopulateGameList();
  QDialog::show();
}

void NetPlaySetupDialog::accept()
{
  SaveSettings();
  if (m_tab_widget->currentIndex() == 0)
  {
    emit Join();
  }
  else
  {
    auto items = m_host_games->selectedItems();
    if (items.empty())
    {
      ModalMessageBox::critical(this, tr("Error"), tr("You must select a game to host!"));
      return;
    }

    if (m_host_server_browser->isChecked() && m_host_server_name->text().isEmpty())
    {
      ModalMessageBox::critical(this, tr("Error"), tr("You must provide a name for your session!"));
      return;
    }

    if (m_host_server_browser->isChecked() &&
        m_host_server_region->currentData().toString().isEmpty())
    {
      ModalMessageBox::critical(this, tr("Error"),
                                tr("You must provide a region for your session!"));
      return;
    }

    emit Host(*items[0]->data(Qt::UserRole).value <std::shared_ptr<const UICommon::GameFile>>());
  }
}

void NetPlaySetupDialog::PopulateGameList()
{
  QSignalBlocker blocker(m_host_games);

  m_host_games->clear();
  for (int i = 0; i < m_game_list_model.rowCount(QModelIndex()); i++)
  {
    std::shared_ptr<const UICommon::GameFile> game = m_game_list_model.GetGameFile(i);
    if ((m_game_list_model.GetNetPlayName(*game) == "Mario Superstar Baseball (GYQE01)"))
    {
      auto* item =
          new QListWidgetItem(QString::fromStdString(m_game_list_model.GetNetPlayName(*game)));
      item->setData(Qt::UserRole, QVariant::fromValue(std::move(game)));
      m_host_games->addItem(item);
    }
  }

  m_host_games->sortItems();

  const QString selected_game =
      Settings::GetQSettings().value(QStringLiteral("netplay/hostgame"), QString{}).toString();
  const auto find_list = m_host_games->findItems(selected_game, Qt::MatchFlag::MatchExactly);

  if (find_list.count() > 0)
    m_host_games->setCurrentItem(find_list[0]);
}

void NetPlaySetupDialog::ResetTraversalHost()
{
  Config::SetBaseOrCurrent(Config::NETPLAY_TRAVERSAL_SERVER,
                           Config::NETPLAY_TRAVERSAL_SERVER.GetDefaultValue());
  Config::SetBaseOrCurrent(Config::NETPLAY_TRAVERSAL_PORT,
                           Config::NETPLAY_TRAVERSAL_PORT.GetDefaultValue());

  ModalMessageBox::information(
      this, tr("Reset Traversal Server"),
      tr("Reset Traversal Server to %1:%2")
          .arg(QString::fromStdString(Config::NETPLAY_TRAVERSAL_SERVER.GetDefaultValue()),
               QString::number(Config::NETPLAY_TRAVERSAL_PORT.GetDefaultValue())));
}


void NetPlaySetupDialog::RefreshBrowser()
{
  std::map<std::string, std::string> filters;

  if (!m_edit_name->text().isEmpty())
    filters["name"] = m_edit_name->text().toStdString();

  if (true)
    filters["version"] = Common::GetRioRevStr();

  if (!m_radio_all->isChecked())
    filters["password"] = std::to_string(m_radio_private->isChecked());

  if (m_region_combo->currentIndex() != 0)
    filters["region"] = m_region_combo->currentData().toString().toStdString();

  if (m_check_hide_ingame->isChecked())
    filters["in_game"] = "0";

  std::unique_lock<std::mutex> lock(m_refresh_filters_mutex);
  m_refresh_filters = std::move(filters);
  m_refresh_event.Set();
  SaveSettings();
}

void NetPlaySetupDialog::RefreshLoopBrowser()
{
  while (m_refresh_run.IsSet())
  {
    m_refresh_event.Wait();

    std::unique_lock<std::mutex> lock(m_refresh_filters_mutex);
    if (m_refresh_filters)
    {
      auto filters = std::move(*m_refresh_filters);
      m_refresh_filters.reset();

      lock.unlock();

      emit UpdateStatusRequestedBrowser(tr("Refreshing..."));

      NetPlayIndex client;

      auto entries = client.List(filters);

      if (entries)
      {
        emit UpdateListRequestedBrowser(std::move(*entries));
      }
      else
      {
        emit UpdateStatusRequestedBrowser(tr("Error obtaining session list: %1")
                                       .arg(QString::fromStdString(client.GetLastError())));
      }
    }
  }
}

void NetPlaySetupDialog::UpdateListBrowser()
{
  const int session_count = static_cast<int>(m_sessions.size());

  m_table_widget->clear();
  m_table_widget->setColumnCount(7);
  m_table_widget->setHorizontalHeaderLabels({tr("Region"), tr("Name"), tr("Ranked Mode"),
                                             tr("Game Mode"), tr("Password?"), tr("Players"),
                                             tr("Version")});

  auto* hor_header = m_table_widget->horizontalHeader();

  hor_header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  hor_header->setSectionResizeMode(1, QHeaderView::Stretch);
  //hor_header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  //hor_header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  hor_header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
  hor_header->setHighlightSections(false);

  m_table_widget->setRowCount(session_count);

  for (int i = 0; i < session_count; i++)
  {
    const auto& entry = m_sessions[i];

    std::vector<std::string> game_tags = Config::LobbyNameVector(entry.name);

    auto* region = new QTableWidgetItem(QString::fromStdString(entry.region));
    auto* name = new QTableWidgetItem(QString::fromStdString(game_tags[0]));
    auto* password = new QTableWidgetItem(entry.has_password ? tr("Yes") : tr("No"));
    auto* is_ranked = new QTableWidgetItem(game_tags[1] == "Ranked" ? tr("Ranked") : tr("Unranked"));
    auto* gamemode = new QTableWidgetItem(QString::fromStdString(game_tags[2]));
    auto* player_count = new QTableWidgetItem(QStringLiteral("%1").arg(entry.player_count));
    auto* version = new QTableWidgetItem(QString::fromStdString(entry.version));

    const bool enabled = Common::GetRioRevStr() == entry.version;

    for (const auto& item : {region, name, is_ranked, gamemode, password, player_count, version})
      item->setFlags(enabled ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags);

    m_table_widget->setItem(i, 0, region);
    m_table_widget->setItem(i, 1, name);
    m_table_widget->setItem(i, 2, is_ranked); // was in_game
    m_table_widget->setItem(i, 3, gamemode);   // was game_id
    m_table_widget->setItem(i, 4, password);
    m_table_widget->setItem(i, 5, player_count);
    m_table_widget->setItem(i, 6, version);
  }

  m_status_label->setText(
      (session_count == 1 ? tr("%1 session found") : tr("%1 sessions found")).arg(session_count));

  m_online_count->setText((Config::ONLINE_COUNT == 1 ? tr("There is %1 player in a lobby") :
                                               tr("There are %1 players in a lobby"))
                              .arg(Config::ONLINE_COUNT));
}

void NetPlaySetupDialog::OnSelectionChangedBrowser()
{
  return;
}

void NetPlaySetupDialog::OnUpdateStatusRequestedBrowser(const QString& status)
{
  m_status_label->setText(status);
}

void NetPlaySetupDialog::OnUpdateListRequestedBrowser(std::vector<NetPlaySession> sessions)
{
  m_sessions = std::move(sessions);
  UpdateListBrowser();
}

void NetPlaySetupDialog::acceptBrowser()
{
  if (m_table_widget->selectedItems().isEmpty())
    return;

  const int index = m_table_widget->selectedItems()[0]->row();

  NetPlaySession& session = m_sessions[index];

  std::string server_id = session.server_id;

  if (m_sessions[index].has_password)
  {
    auto* dialog = new QInputDialog(this);

    dialog->setWindowFlags(dialog->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    dialog->setWindowTitle(tr("Enter password"));
    dialog->setLabelText(tr("This session requires a password:"));
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setTextEchoMode(QLineEdit::Password);

    if (dialog->exec() != QDialog::Accepted)
      return;

    const std::string password = dialog->textValue().toStdString();

    auto decrypted_id = session.DecryptID(password);

    if (!decrypted_id)
    {
      ModalMessageBox::warning(this, tr("Error"), tr("Invalid password provided."));
      return;
    }

    server_id = decrypted_id.value();
  }

  QDialog::accept();

  Config::SetBaseOrCurrent(Config::NETPLAY_TRAVERSAL_CHOICE, session.method);

  Config::SetBaseOrCurrent(Config::NETPLAY_CONNECT_PORT, session.port);

  if (session.method == "traversal")
    Config::SetBaseOrCurrent(Config::NETPLAY_HOST_CODE, server_id);
  else
    Config::SetBaseOrCurrent(Config::NETPLAY_ADDRESS, server_id);

  emit JoinBrowser();
}

std::string NetPlaySetupDialog::LobbyNameString()
{
  std::string lobby_string = m_host_server_name->text().toStdString();
  std::string delimiter = "%%";
  lobby_string += delimiter;
  lobby_string += m_host_ranked->isChecked() ? "Ranked" : "Unranked";
  lobby_string += delimiter;
  lobby_string += m_host_game_mode->currentText().toStdString();
  // potentially add more here for each tag
  return lobby_string;
}
