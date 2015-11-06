#ifndef NFD_DAEMON_FW_MY_RONR_NAM_HPP
#define NFD_DAEMON_FW_MY_RONR_NAM_HPP

#include "strategy.hpp"
#include "my_logger.hpp"
#include "my_panini_fib.hpp"

#include <ndn-cxx/face.hpp>

namespace nfd
{
namespace fw
{

class my_panini: public Strategy
{
public:
    my_panini(Forwarder& forwarder, const ndn::Name& name = STRATEGY_NAME);

    virtual
    ~my_panini();

    virtual void
    afterReceiveInterest(const Face& inFace,
                         const Interest& interest,
                         shared_ptr<fib::Entry> fibEntry,
                         shared_ptr<pit::Entry> pitEntry) DECL_OVERRIDE;

    virtual void
    beforeSatisfyInterest(shared_ptr<pit::Entry> pitEntry,
                                   const Face& inFace, const Data& data) DECL_OVERRIDE;


public:
    static const Name STRATEGY_NAME;

private:
    my_logger m_my_logger;
    my_panini_fib m_my_panini_fib;
    my_panini_fib m_my_nac_fib;
};



} // namespace fw
} // namespace nfd

#endif // NFD_DAEMON_FW_MY_RONR_NAM_HPP
