#ifndef __CTRL_GPIO_H_INCLUDED__
#define	__CTRL_GPIO_H_INCLUDED__


//	cd /sys/class/gpio

//	echo 200 > /sys/class/gpio/export
//	echo out > /sys/class/gpio/gpio200/direction
//	echo 1 > /sys/class/gpio/gpio200/value
//	echo 0 > /sys/class/gpio/gpio200/value
//	echo 200 > /sys/class/gpio/unexport

#include	<fstream>
#include	<iostream>


class GpioOut
{
public:
	GpioOut(int pin)
	{
		m_nPin	= pin;

		if( 0 <= m_nPin )
		{
			std::string PinName = std::to_string(m_nPin);
	
			m_strPath	 = "/sys/class/gpio/gpio" + PinName + "/";
			
			std::ofstream( "/sys/class/gpio/export" ) << PinName;
			std::ofstream( m_strPath+"direction" ) << "out";
			std::ofstream( m_strPath+"value" ) << "0";
		}
	}

	~GpioOut()
	{
		if( 0 <= m_nPin )
		{
			std::ofstream( m_strPath+"value" ) << "0";
			std::ofstream( "/sys/class/gpio/unexport") << std::to_string(m_nPin);
		}
	}

	void    operator << ( int i )
	{
		if( !m_strPath.empty() )
		{
			std::ofstream( m_strPath+"value" ) << std::to_string(i);
		}
	}
	
protected:
	int			m_nPin;
	std::string m_strPath;
};


class GpioIn
{
public:
	GpioIn(int pin)
	{
		std::string PinName = std::to_string(pin);

		m_nPin		= pin;
		m_strPath	 = "/sys/class/gpio/gpio" + PinName + "/";
		
		std::ofstream( "/sys/class/gpio/export" ) << PinName;
		std::ofstream( m_strPath+"direction" ) << "in";
	}

	~GpioIn()
	{
		std::ofstream( "/sys/class/gpio/unexport")  << std::to_string(m_nPin);
	}

	void    operator >> ( int& i )
	{
		std::string	buf;

		std::getline( std::ifstream( m_strPath+"value" ), buf );

		i	= std::stoi(buf );
	}
	
protected:
	int			m_nPin;
	std::string m_strPath;
};



#include <thread>
#include <functional>
#include <map>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>


class GpioInterruptCtrl
{
public:
	GpioInterruptCtrl()
	{
		m_isExecuting	= false;

		m_epfd	= epoll_create(1);
		if (m_epfd < 0)
		{
			throw	"ERROR: epoll_create()";
		}
	}

	~GpioInterruptCtrl()
	{
		ThreadStop();

		::close( m_epfd );
		m_epfd	= -1;

		for( auto& pin : m_iGpioInfo )
		{
			std::ofstream( "/sys/class/gpio/export" ) << std::to_string(pin.first);
		}
	}

	bool    RegistPin( int pin, std::function<void(int)> func )
	{
		if( 0 <= pin )
		{
			tagGpioCtrlInfo*	ptInfo	= NULL;
	
			if( m_iGpioInfo.end() == m_iGpioInfo.find( pin ) )
			{
				tagGpioCtrlInfo	tInfo	= {0};
				std::string 	PinName	= std::to_string(pin);
				std::string		strPath	= "/sys/class/gpio/gpio" + PinName + "/";
	
				// initialize gpio
				std::ofstream( "/sys/class/gpio/export" ) << PinName;
				std::ofstream( strPath+"direction" ) << "in";
				std::ofstream( strPath+"edge" ) << "both";
	
				// open gpio value
				tInfo.pin	= pin;
				tInfo.func	= func;
				tInfo.fd	= ::open( (strPath+"value").c_str(), O_RDWR | O_NONBLOCK);
#ifndef GPIO_BUTTON_POLARITY_INVERT
				tInfo.prevValue = tInfo.value = 0;
#else
				tInfo.prevValue = tInfo.value = 1;
#endif
				tInfo.prevCnt = 0;

				if( tInfo.fd < 0 )
				{
					printf( "ERROR: GpioInterruptCtrl(), open(%s) failured.\n", (strPath+"value").c_str() );
					return	false;	
				}
	
				// read test & dummy read
				{
					char	val;
					if( read( tInfo.fd, &val, 1 ) < 1 )
					{
						printf( "ERROR: GpioInterruptCtrl(), read(%s) failured.\n", (strPath+"value").c_str() );
						return	false;	
					}
				}
	
				// allcoate
				ptInfo		= &(*m_iGpioInfo.insert( std::map<int,tagGpioCtrlInfo>::value_type(pin,tInfo) ).first).second;
			}
			else
			{
				printf( "ERROR: GpioInterruptCtrl() already registed. gpio = %d\n", pin );
				return	false;
			}
	
			struct epoll_event	tEvent	= {0};
			tEvent.events	= EPOLLET;
			tEvent.data.ptr	= ptInfo;
			
			int	ret	= epoll_ctl( m_epfd, EPOLL_CTL_ADD, ptInfo->fd, &tEvent );
			if (0 != ret )
			{
				printf( "ERROR: GpioInterruptCtrl(), epoll_ctl failured. gpio = %d\n", pin );
				return	false;
			}

			return	true;
		}

		return	false;
	}
	
	int	WaitGpioEvent( int timeout )
	{
		struct epoll_event	tEvents[16]	= {0};

		int	nEvents	= epoll_wait( m_epfd, tEvents, sizeof(tEvents)/sizeof(tEvents[0]), 100 );
		for( int i = 0; i < nEvents; i++  )
		{
			volatile tagGpioCtrlInfo*	ptInfo	= (tagGpioCtrlInfo*)tEvents[i].data.ptr;
			char				value;
			
			lseek( ptInfo->fd, 0, SEEK_SET );
			if ( 0 < read( ptInfo->fd, &value, 1 ) )
			{
				std::lock_guard<std::mutex> lock(m_mtx);
				ptInfo->prevValue = value - '0';
				ptInfo->prevCnt = 0;
			}
		}
		
		return	nEvents;
	}

	int	WaitGpioEvent_poll( int timeout )
	{
		for( auto &it : m_iGpioInfo )
		{
			volatile tagGpioCtrlInfo*	ptInfo = &it.second;
			std::lock_guard<std::mutex> lock(m_mtx);
			if (ptInfo->prevValue != ptInfo->value) {
				ptInfo->prevCnt++;
				if (ptInfo->prevCnt >= 2) {
					ptInfo->value = ptInfo->prevValue;
#ifndef GPIO_BUTTON_POLARITY_INVERT
					it.second.func( ptInfo->value );
#else
					it.second.func( ptInfo->value ? 0 : 1 );
#endif
				}
			}else{
				ptInfo->prevCnt = 0;
			}
		}
		
		std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
		return 0;
	}

	bool    ThreadStart( )
	{
		m_isExecuting	= true;
		m_iThread_irq		= std::thread(ThreadProc_irq,this);
		m_iThread_chatter	= std::thread(ThreadProc_chatter,this);
	}
	
	void    ThreadStop()
	{
		if( m_isExecuting )
		{
			m_isExecuting	= false;	
			m_iThread_irq.join();
			m_iThread_chatter.join();
		}
	}
	
protected:
	typedef struct tagGpioCtrlInfo
	{
		int							pin;
		int							fd;
		std::function<void(int)>	func;

		int				value;
		int				prevValue;
		int				prevCnt;
	} tagGpioCtrlInfo;
	
protected:
	static  void    ThreadProc_irq( GpioInterruptCtrl * piThis )
	{
		while( piThis->m_isExecuting )
		{
			// epoll
			piThis->WaitGpioEvent( 250 );
		}
	}

	static  void    ThreadProc_chatter( GpioInterruptCtrl * piThis )
	{
		while( piThis->m_isExecuting )
		{
			// polling
			piThis->WaitGpioEvent_poll( 20 );
		}
	}

protected:
	int								m_epfd;
	bool							m_isExecuting;
	std::thread						m_iThread_irq;
	std::thread						m_iThread_chatter;
	std::map<int,tagGpioCtrlInfo>	m_iGpioInfo;
	std::mutex m_mtx;
};

#endif	//__CTRL_GPIO_H_INCLUDED__
