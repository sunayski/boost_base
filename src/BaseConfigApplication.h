#ifndef BASE_CONFIG_APPLICATION_H
#define BASE_CONFIG_APPLICATION_H

#include <AApplication.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <list>

class Config;
class ConfigItem;
class ThreadPool;

class BaseConfigApplication : public AApplication {
public:
    BaseConfigApplication(const std::string&,
                          int argc, char** argv);
    ~BaseConfigApplication();

    int exec() override final;

    Config* config();

    template <class Callable>
    void post(Callable&& c) {
        boost::asio::post(io(), std::forward<Callable>(c));
    }

protected:
    typedef std::list<std::unique_ptr<ConfigItem>> TConfigItems;

    virtual bool start(TConfigItems&) = 0;
    virtual void doExit() {}
    virtual ThreadPool* createThreadPool();

    boost::asio::io_context &io() const;

private:
    void onExitRequest() override final;
    bool processArgs();

    std::string m_configPath;
    std::unique_ptr<Config> m_config;
    std::shared_ptr<ThreadPool> m_threadPool;
};

#endif // BASE_CONFIG_APPLICATION_H
