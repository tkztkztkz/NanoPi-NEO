#ifndef MUSICCONTROLLERVOLUMIOSIO
#define MUSICCONTROLLERVOLUMIOSIO

#include "MusicController.hpp"
#include "sio_client.h"
#include <thread>


const int m_iPollTime = 50;
const int m_iElapseUpdateTime = 1000;

class MusicList
{
public:
	bool isSong;
	std::string name;
	std::string uri;

	MusicList(bool ty, std::string nm, std::string ur)
	{
		isSong = ty;
		name = nm;
		uri = ur;
	}
};

class MusicControllerVolumioSIO : public MusicController
{
private:
	sio::client m_v2sio;
	int m_iPortImage;
	std::string m_nAlbumart;
	std::string m_nHostname;

	std::thread m_iThread;
	std::chrono::system_clock::time_point m_iPrevPollTime;
	int m_iCoverartRetryCnt;

	struct pushHandler {
		bool isRecv;
		int  status;
	};
	std::map<std::string, pushHandler> m_iPushHnd;

public:
	MusicControllerVolumioSIO();
	int connect();
	int disconnect();
	int toggle();
	int next();
	int prev();
	int volume(int);
	int volumeoffset(int);

	int getPlayPos();
	int setPlayQueue(int index);
	int setPlayList (int index);
	int createPlayList (std::string &name);
	int addToPlaylist(std::string name, std::string service, std::string uri);


	// volumio specific fuction(Music Library)
	int emitVolumio(std::string cmd);
	int emitVolumio(std::string cmd, std::string key, std::string data);
	int emitVolumio(std::string cmd, std::string key, int data);
	int addToQueue (std::string &uri);
	std::vector<MusicList> browseLibrary (std::string &uri);
	std::vector<MusicList> m_iLib;
	bool m_isLibFlag;
	int addWebRadio(std::string name, std::string uri);

	MenuList *getMenu(MenuController &iCtl);

	bool m_isConnected;
	void pollEvent();

	int initPushHandler(std::string tag);
	int waitPushHandler(std::string tag);

private:
	int callMethod(std::string endpoint, std::string method, sio::message::ptr data);
	int callMethod(const char* endpoint, const char* method, sio::message::ptr data);
};

#endif

