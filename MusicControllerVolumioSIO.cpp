#include "MusicController.hpp"
#include "MusicControllerVolumioSIO.hpp"
#define MUSIC_ROOT_PATH "/mnt/"
#include <iostream>
#include <thread>

#include "common/ctrl_http_curl.h"


MusicControllerVolumioSIO::MusicControllerVolumioSIO(): MusicController()
{
	m_iPort = 3000;
	m_iPortImage = 3001;

	m_isConnected = false;
	m_iCoverartRetryCnt = 0;

	// override capability
	m_hasQueue = true;
	m_hasPlaylists = true;
}

static void ThreadProc(MusicControllerVolumioSIO * piThis)
{
	while(piThis->m_isConnected)
	{
		piThis->pollEvent();
	}
}

static sio::message::ptr findObject(sio::message::ptr msg, std::string objname)
{
	switch (msg->get_flag())
	{
	case sio::message::flag_array:
//		std::cout << "array: " << msg->get_vector().size() << std::endl;
		for ( auto it: msg->get_vector()) {
			sio::message::ptr ret = findObject(it, objname);
			if (ret != nullptr) return ret;
		}
		break;
	case sio::message::flag_object:
//		std::cout << "object: " << msg->get_map().size() << std::endl;
		for ( auto it: msg->get_map()) {
//			std::cout << " map: " << it.first << std::endl;
			if ( it.first == objname) {
				return it.second;
			}else{
				sio::message::ptr ret = findObject(it.second, objname);
				if (ret != nullptr) return ret;
			}
		}
		break;
	}

	return nullptr;
}

static std::string getStringFromObject(sio::message::ptr msg, std::string mapname)
{
	std::string ret = "";
	if (msg->get_flag() == sio::message::flag_object ) {
//		std::cout << "object: " << msg->get_map().size() << std::endl;
		for ( auto it: msg->get_map()) {
//			std::cout << " map: " << it.first << std::endl;
			if ( it.first == mapname && it.second->get_flag() == sio::message::flag_string) {
				ret = it.second->get_string();
				break;
			}
		}
	}

	return ret;
}

static void show_json_tree(sio::message::ptr msg, int indent = 0)
{
	std::string spc;
	spc.insert(0,"              ",indent);
	switch (msg->get_flag())
	{
	case sio::message::flag_integer:
		std::cout << spc << "integer: " << msg->get_int() << std::endl;
		break;
	case sio::message::flag_string:
		std::cout << spc << "string: " << msg->get_string() << std::endl;
		break;
	case sio::message::flag_boolean:
		std::cout << spc << "bool: " << msg->get_bool() << std::endl;
		break;
	case sio::message::flag_double:
		std::cout << spc << "double: " << msg->get_double() << std::endl;
		break;
	case sio::message::flag_array:
		std::cout << spc << "array: " << msg->get_vector().size() << std::endl;
		for ( auto it: msg->get_vector()) {
			show_json_tree(it, indent + 1);
		}
		break;
	case sio::message::flag_object:
		std::cout << spc << "object: " << msg->get_map().size() << std::endl;
		for ( auto it: msg->get_map()) {
			std::cout << spc << " map: " << it.first << std::endl;
			show_json_tree(it.second, indent + 1);
		}
		break;
	}
}

int MusicControllerVolumioSIO::connect()
{
	m_iState.clear();
	// register callback
	m_v2sio.socket()->on("pushState", [&](sio::event &ev) {
		std::cout << ev.get_name() << std::endl;
		sio::message::ptr msg = ev.get_message();

		std::lock_guard<std::mutex> lock(m_mtx);
		m_iPrevPollTime = std::chrono::system_clock::now();
		m_iState.clear();
		/* parse JSON */
		for (auto it : msg->get_map())
		{
			switch (it.second->get_flag())
			{
			case sio::message::flag_integer:
				m_iState[it.first] = std::to_string(it.second->get_int());
				break;
			case sio::message::flag_string:
				m_iState[it.first] = it.second->get_string();
				break;
			case sio::message::flag_boolean:
				m_iState[it.first] = it.second->get_bool() ? "true" : "false";
				break;
			case sio::message::flag_double:
				m_iState[it.first] = std::to_string(it.second->get_double());
				break;
			}
		}
		/* map to mpd status */
		m_iState["state"] = m_iState["status"].empty() ? " " : m_iState["status"];
		m_iState["Title"] = m_iState["title"].empty() ? " " : m_iState["title"];
		m_iState["Album"] = m_iState["album"].empty() ? " " : m_iState["album"];
		m_iState["Artist"] = m_iState["artist"].empty() ? " " : m_iState["artist"];
		if (!m_iState["duration"].empty()) m_iState["Time"] = std::to_string(std::stoi(m_iState["duration"]) * 1000);
		if (!m_iState["seek"].empty()) m_iState["elapsed"] = m_iState["seek"];
		if (!m_iState["uri"].empty())
		{ // normal mode
			m_iState["file"] = m_iState["uri"];
		}
		else
		{ // airplay mode
			m_iState["file"] = m_iState["Title"] + m_iState["Album"] + m_iState["Artist"];
		}
		if (m_iState["updatedb"] == "true") {
			m_iState["connected"] = "updateDB";
		}else{
			m_iState["connected"] = "connected";
		}
		m_iState["hostname"]  = m_nHostname;
		m_iState["pcmrate"] = m_iState["samplerate"] + " / " + m_iState["bitdepth"];
		m_isUpdateState = true;
		m_iCoverartRetryCnt = 0;
//		for (auto it : m_iState)	std::cout << it.first << " : " << it.second << std::endl;
	});

	m_v2sio.socket()->on("pushQueue", [&](sio::event &ev) {
		std::cout << ev.get_name() << std::endl;
		sio::message::ptr msg = ev.get_message();

		std::lock_guard<std::mutex> lock(m_mtx);
		m_iQueue.clear();
		/* parse JSON */
		for (auto itv : msg->get_vector())
		{
			std::map<std::string, std::string> iQueue;
			for (auto itm : itv->get_map())
			{
				switch (itm.second->get_flag())
				{
				case sio::message::flag_integer:
					iQueue[itm.first] = std::to_string(itm.second->get_int());
					break;
				case sio::message::flag_string:
					iQueue[itm.first] = itm.second->get_string();
					break;
				case sio::message::flag_boolean:
					iQueue[itm.first] = itm.second->get_bool() ? "true" : "false";
					break;
				case sio::message::flag_double:
					iQueue[itm.first] = std::to_string(itm.second->get_double());
					break;
				}
			}
			m_iQueue.push_back(iQueue);
		}
		m_isUpdateQueue = true;
	});

	m_v2sio.socket()->on("pushListPlaylist", [&](sio::event &ev) {
		std::cout << ev.get_name() << std::endl;
		sio::message::ptr msg = ev.get_message();

		std::lock_guard<std::mutex> lock(m_mtx);
		m_iPlaylist.clear();
		/* parse JSON */
		for (auto it : msg->get_vector())
		{
			switch (it->get_flag())
			{
			case sio::message::flag_string:
				m_iPlaylist.push_back(it->get_string());
				break;
			default:
				std::cout << ev.get_name() << ": parse error" << std::endl;
				break;
			}
		}
		m_isUpdatePlaylists = true;
//		for (auto it : m_iPlaylist)	std::cout << it << std::endl;
	});

	m_v2sio.socket()->on("pushDeviceInfo", [&](sio::event &ev) {
		std::cout << ev.get_name() << std::endl;
		sio::message::ptr msg = ev.get_message();

		/* parse JSON */
		m_nHostname = msg->get_map().count("name") ? msg->get_map()["name"]->get_string() : "";
	});

	m_v2sio.socket()->on("pushOutputDevices", [&](sio::event &ev) {
		std::cout << ev.get_name() << std::endl;
		sio::message::ptr msg = ev.get_message();

		/* parse JSON */
		show_json_tree(msg);
	});

	m_v2sio.socket()->on("pushMethod", [&](sio::event &ev) {
		std::cout << ev.get_name() << std::endl;
		sio::message::ptr msg = ev.get_message();

		/* parse JSON */
		show_json_tree(msg);
	});

	m_v2sio.socket()->on("pushAudioOutputs", [&](sio::event &ev) {
		std::cout << ev.get_name() << std::endl;
		sio::message::ptr msg = ev.get_message();

		/* parse JSON */
		show_json_tree(msg);
	});

	m_v2sio.socket()->on("pushBrowseSources", [&](sio::event &ev) {
		std::cout << ev.get_name() << std::endl;
		sio::message::ptr msg = ev.get_message();

		/* parse JSON */
		show_json_tree(msg);
	});

	m_v2sio.socket()->on("pushBrowseLibrary", [&](sio::event &ev) {
		std::cout << ev.get_name() << std::endl;
		sio::message::ptr msg = ev.get_message();

		/* parse JSON */
		sio::message::ptr obj = findObject(msg, "items");
		if (obj != nullptr) {
			std::cout << "found items" << std::endl;
			std::lock_guard<std::mutex> lock(m_mtx);
			m_iLib.clear();

			if ( obj->get_flag() ==sio::message::flag_array ) {
				std::cout << "array: " << obj->get_vector().size() << std::endl;
				for ( auto it: obj->get_vector()) {
					std::string title = getStringFromObject(it, "title");
					std::string type  = getStringFromObject(it, "type");
					std::string uri   = getStringFromObject(it, "uri");
					bool type_song = ( type == "song") || ( type == "webradio" );
					std::cout << "title: " << title << "  type: " << type << "  uri: " << uri << std::endl;
					m_iLib.push_back(MusicList(type_song, title, uri));
				}
			}
			m_isLibFlag = true;
		}
	});

	m_v2sio.set_socket_open_listener([&](std::string const &nsp) {
		std::cout << "socket open" << std::endl;
		m_v2sio.socket()->emit("getDeviceInfo");
		m_v2sio.socket()->emit("getQueue");
		m_v2sio.socket()->emit("listPlaylist");
//			m_v2sio.socket()->emit("getOutputDevices");
//			m_v2sio.socket()->emit("getAudioOutputs");
//			m_v2sio.socket()->emit("getBrowseSources");

//			callMethod("audio_interface/alsa_controller", "getAudioDevices", sio::null_message::create());
		m_v2sio.socket()->emit("getState");
	});

	m_v2sio.set_reconnect_listener([&](unsigned int retry, unsigned int retrywait) {
		std::lock_guard<std::mutex> lock(m_mtx);
		if (retry > 0)
		{
			m_iState.clear();
		}
		m_iState["connected"] = "connecting...";
		m_isUpdateState = true;
	});

	m_v2sio.set_socket_close_listener([&](std::string const &nsp) {
		{
			std::cout << "socket close" << std::endl;
			std::lock_guard<std::mutex> lock(m_mtx);
			m_iState.clear();
			m_iState["connected"] = "shutdown...";
			m_isUpdateState = true;
		}
	});
#if 0
    m_v2sio.set_open_listener([&]()
    {
            std::cout << "open" << std::endl;
    });

    m_v2sio.set_fail_listener([&]()
    {
            std::cout << "fail" << std::endl;
    });

    m_v2sio.set_reconnecting_listener([&]()
    {
            std::cout << "reconnecting" << std::endl;
    });

    m_v2sio.set_close_listener([&](sio::client::close_reason const& reason)
    {
            std::cout << "close: " << reason << std::endl;
    });
#endif

	// connect
	std::lock_guard<std::mutex> lock(m_mtx);
	m_iState["connected"] = "connecting...";
	m_isUpdateState = true;

	std::string uri = "http://" + m_iHost + ":" + std::to_string(m_iPort);
	m_v2sio.connect(uri);
	m_isConnected = true;
	m_iThread = std::thread(ThreadProc,this);
}

int MusicControllerVolumioSIO::disconnect()
{
	m_isConnected = false;
	m_iThread.join();
	m_v2sio.close();
	return 0;
}

int MusicControllerVolumioSIO::toggle()
{
	m_v2sio.socket()->emit("toggle");
	m_v2sio.socket()->emit("getQueue");
	return 0;
}

int MusicControllerVolumioSIO::next()
{
	m_v2sio.socket()->emit("next");
	return 0;
}

int MusicControllerVolumioSIO::prev()
{
	m_v2sio.socket()->emit("prev");
	return 0;
}

int MusicControllerVolumioSIO::volume(int vol)
{
	std::lock_guard<std::mutex> lock(m_mtx);
	m_v2sio.socket()->emit("volume", sio::int_message::create(vol));
	return vol;
}

int MusicControllerVolumioSIO::volumeoffset(int offset)
{
	int value;
	{
		std::lock_guard<std::mutex> lock(m_mtx);
		if (m_iState["volume"].empty()) return -1;
		value = std::stoi(m_iState["volume"]) + offset;
		value = 0 <= value ? value <= 100 ? value : 100 : 0;
		m_v2sio.socket()->emit("volume", sio::int_message::create(value));
		m_iState["volume"] = std::to_string(value);
	}
	return value;
}

int MusicControllerVolumioSIO::getPlayPos()
{
	std::lock_guard<std::mutex> lock(m_mtx);
	if (m_iState["position"].empty()) {
		return -1;
	}
	return std::stoi(m_iState["position"]);
}

int MusicControllerVolumioSIO::setPlayQueue(int index)
{
	std::lock_guard<std::mutex> lock(m_mtx);
	// emit
	emitVolumio("play", "value", index);
	// update local info
	m_iState["position"] = std::to_string(index);
	return 0;
}
int MusicControllerVolumioSIO::setPlayList (int index)
{
	std::lock_guard<std::mutex> lock(m_mtx);
	return emitVolumio("playPlaylist", "name", m_iPlaylist[index]);
}

int MusicControllerVolumioSIO::addToQueue (std::string &uri)
{
	return emitVolumio("addToQueue", "uri", uri);
}

int MusicControllerVolumioSIO::createPlayList (std::string &name)
{
	initPushHandler("pushCreatePlaylist");
	emitVolumio("createPlaylist", "name", name);
	waitPushHandler("pushCreatePlaylist");

	return 0;
}


int MusicControllerVolumioSIO::emitVolumio(std::string cmd)
{
	// emit
	m_v2sio.socket()->emit(cmd);
	return 0;
}

int MusicControllerVolumioSIO::emitVolumio(std::string cmd, std::string key, std::string data)
{
	// setup parameter
	sio::message::ptr send_data(sio::object_message::create());
	std::map<std::string, sio::message::ptr>& map = send_data->get_map();
	map.insert(std::make_pair(key, sio::string_message::create(data)));
	// emit
	m_v2sio.socket()->emit(cmd, send_data);
	return 0;
}

int MusicControllerVolumioSIO::emitVolumio(std::string cmd, std::string key, int data)
{
	// setup parameter
	sio::message::ptr send_data(sio::object_message::create());
	std::map<std::string, sio::message::ptr>& map = send_data->get_map();
	map.insert(std::make_pair(key, sio::int_message::create(data)));
	// emit
	m_v2sio.socket()->emit(cmd, send_data);
	return 0;
}

void MusicControllerVolumioSIO::pollEvent()
{
	std::this_thread::sleep_for(std::chrono::milliseconds(m_iPollTime));
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

	// update elapsed value
	static std::chrono::system_clock::time_point el_time;
	if (std::chrono::duration_cast<std::chrono::milliseconds>(now - el_time).count() > m_iElapseUpdateTime){
		std::lock_guard<std::mutex> lock(m_mtx);
		el_time = now;
		if (!m_iState["state"].empty() && m_iState["state"] == "play") {
			int elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
													now - m_iPrevPollTime).count();

			elapsed += std::stoi(m_iState["seek"]);
			m_iState["elapsed"] = std::to_string(elapsed);
			m_isUpdateState = true;
		}
	}

	// get coverart
	if ( m_iCoverartRetryCnt < 3 && m_nAlbumart != m_iState["albumart"] ) {
		int ret;
		std::vector<unsigned char> iData;
		std::string url = m_iState["albumart"];
		if (url.substr(0, 4) == "http") {
			ret = HttpCurl::Get(url, iData);
		}else{
			std::string str = "http://" + m_iHost + ":" +  std::to_string(m_iPortImage) + url;
			ret = HttpCurl::Get(str, iData);
		}
		if (ret == 0 && iData.size() > 1024)
		{
			std::lock_guard<std::mutex> lock(m_mtx);
			std::cout << "getAlbumart" << " : " << iData.size() << std::endl;
			m_iCoverart.clear();
			m_iCoverart = iData;
			m_nAlbumart = url;
			m_isUpdateCoverart = true;
		}
		else
		{
			std::lock_guard<std::mutex> lock(m_mtx);
			m_iCoverart.clear();
			std::cout << "getAlbumart failed size = " << iData.size() <<  std::endl;
			m_nAlbumart = "";
			m_iCoverartRetryCnt++;
			m_isUpdateCoverart = false;
		}
	}
}

int MusicControllerVolumioSIO::addWebRadio(std::string name, std::string uri)
{
	sio::message::ptr send_data(sio::object_message::create());
	std::map<std::string, sio::message::ptr>& map = send_data->get_map();
	map.insert(std::make_pair("service", sio::string_message::create("webradio")));
	map.insert(std::make_pair("title", sio::string_message::create(name)));
	map.insert(std::make_pair("uri", sio::string_message::create(uri)));
	m_v2sio.socket()->emit("addPlay", send_data);

	return 0;
}

int MusicControllerVolumioSIO::addToPlaylist(std::string name, std::string service, std::string uri)
{
	sio::message::ptr send_data(sio::object_message::create());
	std::map<std::string, sio::message::ptr>& map = send_data->get_map();
	map.insert(std::make_pair("name", sio::string_message::create(name)));
	map.insert(std::make_pair("service", sio::string_message::create(service)));
	map.insert(std::make_pair("uri", sio::string_message::create(uri)));

	initPushHandler("pushAddToPlaylist");
	m_v2sio.socket()->emit("addToPlaylist", send_data);
	waitPushHandler("pushAddToPlaylist");

	return 0;
}

// "endpoint":"audio_interface/alsa_controller", "method":"saveAlsaOptions"}
int MusicControllerVolumioSIO::callMethod(std::string endpoint, std::string method, sio::message::ptr data)
{
	sio::message::ptr send_data(sio::object_message::create());
	std::map<std::string, sio::message::ptr>& map = send_data->get_map();
	map.insert(std::make_pair("endpoint", sio::string_message::create(endpoint)));
	map.insert(std::make_pair("method", sio::string_message::create(method)));
	if (data == nullptr) data = sio::null_message::create();
	map.insert(std::make_pair("data", data));
	m_v2sio.socket()->emit("callMethod", send_data);

	return 0;
}
int MusicControllerVolumioSIO::callMethod(const char* endpoint, const char* method, sio::message::ptr data)
{
	return callMethod(std::string(endpoint), std::string(method), data);
}


std::vector<MusicList> MusicControllerVolumioSIO::browseLibrary (std::string &uri)
{
	std::vector<MusicList> list;
	// emit cmd
	{
		std::lock_guard<std::mutex> lock(m_mtx);
		m_iLib.clear();
		m_isLibFlag = false;
		std::cout << "browseLibrary: " << uri << std::endl;
		emitVolumio("browseLibrary", "uri", uri);
	}

	// sync data
	for (int i = 0; i < 50; i++) {
		{
			std::lock_guard<std::mutex> lock(m_mtx);
			if (m_isLibFlag) {
				list = m_iLib;
				break;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	return list;
}


int MusicControllerVolumioSIO::initPushHandler(std::string tag)
{
	std::lock_guard<std::mutex> lock(m_mtx);
	if (m_iPushHnd.count(tag) == 0) {
		m_iPushHnd[tag].isRecv = false;
		m_v2sio.socket()->on(tag, [&](sio::event &ev) {
			std::cout << ev.get_name() << std::endl;
			sio::message::ptr msg = ev.get_message();
			std::lock_guard<std::mutex> lock(m_mtx);
			m_iPushHnd[ev.get_name()].isRecv = true;
	        });
	}
	m_iPushHnd[tag].isRecv = false;
	m_iPushHnd[tag].status = -1;

	return 0;
}
int MusicControllerVolumioSIO::waitPushHandler(std::string tag)
{
	int ret = -1;
        // sync data
        for (int i = 0; i < 50; i++) {
                {
                        std::lock_guard<std::mutex> lock(m_mtx);
                        if (m_iPushHnd[tag].isRecv){
				ret = m_iPushHnd[tag].status;
                                break;
                        }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

	return ret;
}

class MenuMusicLib : public MenuList
{
private:
	std::string m_iUri;
	MusicControllerVolumioSIO *m_iMusic;
	std::vector<MusicList> m_iList;
	bool m_isWebRadio;
public:
	MenuMusicLib(const char* iName, MenuController &iCtl,  MusicControllerVolumioSIO *iMusic, std::string uri, bool isRadio = false ) : MenuList(iName, iCtl)
	{
		m_iMusic = iMusic;
		m_iUri = uri;
		m_isWebRadio = isRadio;
	}

	int enter()
	{
		// clear list
		for (auto it : m_iMenulist)
			delete it.second;
		m_iMenulist.clear();
		menulists.clear();

		// request
		m_iList = m_iMusic->browseLibrary(m_iUri);

		// create list
		if (m_iList.empty()) {
			index = -1;
			return 0;
		}
		if (!m_isWebRadio) {
			menulists.push_back("[Add Queue]"); // Add Queue Menu
		}
		for (auto it: m_iList){
			if (it.isSong) { // Music File
				menulists.push_back(it.name);
			}else{ // Folder
				std::string listname = "<" + it.name +">";
				addmenu(new MenuMusicLib(listname.c_str(), m_iCtl, m_iMusic, it.uri, m_isWebRadio));
			}
		}
	}

	MenuList *exec()
	{
		int l_index = index;
		if ( !m_isWebRadio && index == 0) {
			m_iMusic->addToQueue(m_iUri);
			return nullptr; // leave menu
		}
		if ( !m_isWebRadio ) l_index--;

		if (m_iList[l_index].isSong) { // Music file
			std::cout << "addToQueue: " << m_iList[l_index].name << " : " << m_iList[l_index].uri << " : " << m_isWebRadio << std::endl;
			if (m_isWebRadio) {
				m_iMusic->addWebRadio(m_iList[l_index].name, m_iList[l_index].uri);
			}else{
				m_iMusic->emitVolumio("addPlay", "uri", m_iList[l_index].uri);
			}
			return this;
		}else{ // Folder
			std::cout << "enter: " << m_iList[l_index].name << " : " << m_iList[l_index].uri << std::endl;
			return m_iMenulist[index];
		}
	}
};


class MenuVolumio : public MenuList
{
private:
	MusicControllerVolumioSIO *m_iMusic;
public:
	MenuVolumio(const char* iName, MenuController &iCtl,  MusicControllerVolumioSIO *iMusic) : MenuList(iName, iCtl)
	{
		m_iMusic = iMusic;
		menulists.push_back("Clear Queue");
		addmenu(new MenuMusicLib("Music Library", m_iCtl, iMusic, "music-library"));
		addmenu(new MenuMusicLib(" -artists", m_iCtl, iMusic, "artists://"));
		addmenu(new MenuMusicLib(" -albums", m_iCtl, iMusic, "albums://"));
		addmenu(new MenuMusicLib(" -genres", m_iCtl, iMusic, "genres://"));
		addmenu(new MenuMusicLib("Web Radio", m_iCtl, iMusic, "radio", true));
		menulists.push_back("Create Playlist");
		menulists.push_back("Update DB");
		menulists.push_back("Rescan DB");
	}
	int enter()
	{
		index = 0;
	}

	MenuList *exec()
	{
		switch(index) {
			case 0:
			m_iMusic->emitVolumio("clearQueue");
			break;

			case 6:
			{
				int num = 0;
				std::string playlist;
				std::vector<std::string> iList;
				m_iMusic->getPlayLists(iList);
				int dup;
				do { // check playlist name duplication
					dup = false;
					char cplaylist[32] = {0};
					snprintf(cplaylist, sizeof(cplaylist)-1, "playlist_%05d", num++);
					playlist = cplaylist;
					for (auto it: iList) 
						if (it == playlist) { dup = true; continue; }
				} while(dup);
				m_iMusic->createPlayList(playlist);
				std::vector<std::map<std::string, std::string>> iQueue;
				m_iMusic->getQueue(iQueue);
				for (auto it: iQueue) {
					std::cout << "addToPlaylist: " << playlist << " : " << it["uri"] << std::endl;
					m_iMusic->addToPlaylist(playlist, it["service"], it["uri"]);
				}
//				m_iMusic->emitVolumio("listPlaylist");
			}	
			break;

			case 7:
			m_iMusic->emitVolumio("updateDb");
			break;

			case 8:
			m_iMusic->emitVolumio("rescanDb");
			break;

			default:
			return m_iMenulist[index];
		}
		return nullptr; // leave menu
	}
};

MenuList *MusicControllerVolumioSIO::getMenu(MenuController &iCtl)
{
	return new MenuVolumio("Volumio", iCtl, this);
}
