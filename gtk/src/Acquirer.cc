/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * Acquirer.cc
 * Copyright (C) 2013-2014 Sandro Mani <manisandro@gmail.com>
 *
 * gImageReader is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gImageReader is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Acquirer.hh"
#include "Config.hh"
#include "FileDialogs.hh"
#include "MainWindow.hh"
#include "Utils.hh"

#include <cstring>

Acquirer::Acquirer()
{
	m_devCombo = Builder("combo:sources.acquire.device");
	m_refreshButton = Builder("button:inpute.acquire.device.refresh");
	m_refreshSpinner = Builder("spinner:sources.acquire.device");
	m_resCombo = Builder("combo:sources.acquire.resolution");
	m_modeCombo = Builder("combo:sources.acquire.mode");
	m_msgLabel = Builder("label:sources.acquire.message");
	m_outputLabel = Builder("label:sources.acquire.outputname");
	m_buttonBox = Builder("buttonbox:sources.acquire");
	m_scanButton = Builder("button:sources.acquire.scan");
	m_cancelButton = Gtk::manage(new Gtk::Button(_("Cancel")));
	m_cancelButton->set_image_from_icon_name("dialog-cancel", Gtk::ICON_SIZE_BUTTON);
	m_cancelButton->show();

	m_devCombo->set_model(Gtk::ListStore::create(m_devComboCols));
	Gtk::CellRendererText* cell = Gtk::manage(new Gtk::CellRendererText);
	cell->property_ellipsize() = Pango::ELLIPSIZE_END;
	m_devCombo->pack_start(*cell, true);
	m_devCombo->add_attribute(*cell, "text", 0);

	if(m_resCombo->get_active_row_number() == -1) m_resCombo->set_active(2);
	if(m_modeCombo->get_active_row_number() == -1) m_modeCombo->set_active(0);

	m_scanThread = new ScanThread();

	CONNECT(m_refreshButton, clicked, [this]{ startDetectDevices(); });
	CONNECT(m_scanButton, clicked, [this]{ startScan(); });
	CONNECT(m_cancelButton, clicked, [this]{ cancelScan(); });
	CONNECT(Builder("button:sources.acquire.output").as<Gtk::Button>(), clicked, [this]{ selectOutputPath(); });
	CONNECT(m_devCombo, changed, [this]{ setDeviceComboTooltip(); });
	CONNECT(m_scanThread, initFailed, [this]{ scanInitFailed(); });
	CONNECT(m_scanThread, devicesDetected, [this](const std::vector<ScanBackend::Device>& devices){ doneDetectDevices(devices); });
	CONNECT(m_scanThread, scanFailed, [this](const std::string& msg){ scanFailed(msg); });
	CONNECT(m_scanThread, scanStateChanged, [this](ScanThread::State state){ setScanState(state); });
	CONNECT(m_scanThread, pageAvailable, [this](const std::string& file){ m_signal_scanPageAvailable.emit(file); });

	MAIN->getConfig()->addSetting(new ComboSetting("scanres", "combo:sources.acquire.resolution"));
	MAIN->getConfig()->addSetting(new ComboSetting("scanmode", "combo:sources.acquire.mode"));
	MAIN->getConfig()->addSetting(new VarSetting<Glib::ustring>("scanoutput"));
	MAIN->getConfig()->addSetting(new ComboSetting("scandev", "combo:sources.acquire.device"));

	m_outputPath = MAIN->getConfig()->getSetting<VarSetting<Glib::ustring>>("scanoutput")->getValue();
	if(m_outputPath.empty()) {
		m_outputPath = Glib::build_filename(Utils::get_documents_dir(), _("scan.png"));
	}
	genOutputPath();
	m_thread = Glib::Threads::Thread::create(sigc::mem_fun(m_scanThread, &ScanThread::run));
}

Acquirer::~Acquirer()
{
	m_scanThread->stop();
	if(m_thread != nullptr){
		m_thread->join();
		m_thread = nullptr;
	}
}

void Acquirer::selectOutputPath()
{
	FileDialogs::FileFilter filter = {_("PNG Images"), "image/png", "*.png"};
	std::string filename = FileDialogs::save_dialog(_("Choose Output Filename..."), m_outputPath, filter);
	if(!filename.empty()){
		m_outputPath = filename;
		genOutputPath();
	}
}

void Acquirer::genOutputPath()
{
	m_outputPath = Utils::make_output_filename(m_outputPath);
	m_outputLabel->set_text(m_outputPath);
	m_outputLabel->set_tooltip_text(m_outputPath);
	MAIN->getConfig()->getSetting<VarSetting<Glib::ustring>>("scanoutput")->setValue(m_outputPath);
}

void Acquirer::scanInitFailed()
{
	m_msgLabel->set_markup(Glib::ustring::compose("<span color='red'>%1</span>", _("Failed to initialize the scanning backend.")));
	m_scanButton->set_sensitive(false);
	m_refreshButton->set_sensitive(false);
	m_refreshButton->show();
	m_refreshSpinner->hide();
	m_refreshSpinner->stop();
	m_scanThread->stop();
}

void Acquirer::scanFailed(const Glib::ustring &msg)
{
	m_msgLabel->set_markup(Glib::ustring::compose("<span color='red'>%1: %2</span>", _("Scan failed"), msg));
}

void Acquirer::startDetectDevices()
{
	m_refreshButton->hide();
	m_refreshSpinner->show();
	m_refreshSpinner->start();
	m_msgLabel->set_text("");
	m_devCombo->set_model(Gtk::ListStore::create(m_devComboCols));
	m_scanButton->set_sensitive(false);
	m_scanThread->redetect();
}

void Acquirer::doneDetectDevices(const std::vector<ScanBackend::Device>& devices)
{
	m_refreshButton->show();
	m_refreshSpinner->hide();
	m_refreshSpinner->stop();
	if(devices.empty()){
		m_msgLabel->set_markup(Glib::ustring::compose("<span color='red'>%1</span>", _("No scanners were detected.")));
	}else{
		Glib::RefPtr<Gtk::ListStore> store = Glib::RefPtr<Gtk::ListStore>::cast_static(m_devCombo->get_model());
		for(const ScanBackend::Device& device : devices){
			Gtk::TreeIter it = store->append();
			it->set_value(m_devComboCols.label, device.label);
			it->set_value(m_devComboCols.name, device.name);
		}
		m_devCombo->set_active(0);
		m_scanButton->set_sensitive(true);
	}
}

void Acquirer::startScan()
{
	m_buttonBox->remove(*m_scanButton);
	m_cancelButton->set_sensitive(true);
	m_buttonBox->pack_start(*m_cancelButton, false, true);
	m_msgLabel->set_text(_("Starting scan..."));

	int res[] = {75, 100, 200, 300, 600, 1200};
	ScanBackend::ScanMode modes[] = {ScanBackend::ScanMode::COLOR, ScanBackend::ScanMode::GRAY};
	genOutputPath();
	ScanBackend::Options opts = {m_outputPath, res[m_resCombo->get_active_row_number()], modes[m_modeCombo->get_active_row_number()], 8, ScanBackend::ScanType::SINGLE, 0, 0};
	genOutputPath();
	m_scanThread->scan((*m_devCombo->get_active())[m_devComboCols.name], opts);
}

void Acquirer::setScanState(ScanThread::State state)
{
	if(state == ScanThread::State::OPEN){
		m_msgLabel->set_text(_("Opening device..."));
	}else if(state == ScanThread::State::SET_OPTIONS){
		m_msgLabel->set_text(_("Setting options..."));
	}else if(state == ScanThread::State::START){
		m_msgLabel->set_text(_("Starting scan..."));
	}else if(state == ScanThread::State::GET_PARAMETERS){
		m_msgLabel->set_text(_("Getting parameters..."));
	}else if(state == ScanThread::State::READ){
		m_msgLabel->set_text(_("Transferring data..."));
	}else if(state == ScanThread::State::IDLE){
		doneScan();
	}
}

void Acquirer::cancelScan()
{
	m_scanThread->cancel();
	m_cancelButton->set_sensitive(false);
	m_msgLabel->set_text(_("Canceling scan..."));
}

void Acquirer::doneScan()
{
	m_buttonBox->remove(*m_cancelButton);
	m_buttonBox->pack_start(*m_scanButton, false, true);
	m_msgLabel->set_text("");
}

void Acquirer::setDeviceComboTooltip()
{
	auto it = m_devCombo->get_active();
	m_devCombo->set_tooltip_text(it ? static_cast<std::string>((*it)[m_devComboCols.label]) : "");
}