#include "my_panini.hpp"

#include <ndn-cxx/management/nfd-controller.hpp>

//#include <mutex>
//#include <thread>

#include <cstdlib>

namespace nfd
{
namespace fw
{

//#define MY_DEBUG_OUTPUT
#ifdef MY_DEBUG_OUTPUT
# define DOUT(x) x
#else
# define DOUT(x)
#endif

const Name my_panini::STRATEGY_NAME("ndn:/localhost/nfd/strategy/my_panini/%FD%01");
NFD_REGISTER_STRATEGY(my_panini);

my_panini::my_panini(Forwarder& forwarder, const ndn::Name& name)
    : Strategy(forwarder, name)
    , m_my_logger()
    , m_my_panini_fib()
{
    DOUT(std::cout << "DEBUG: start PANINI Strategy" << std::endl;)

    m_my_panini_fib.set_with_save_probability(false);
    m_my_panini_fib.set_is_nac(false);
    m_my_panini_fib.set_face_limit_per_entry(1); //lokale interfaces koennen ihre id aendern, wodurch sich auf dauer die Fibsize mit lokalen id füllt
    set_extern_panini_fib_parameter();

    m_my_nac_fib.set_is_nac(false);
    m_my_nac_fib.set_with_save_probability(false);
}

my_panini::~my_panini()
{
    DOUT(std::cout << "DEBUG: stopped PANINI Strategy" << std::endl;)
}

void my_panini::set_extern_panini_fib_parameter()
{
    if (const char* env_p = std::getenv("FIB_TABLE_SIZE")) {
        try {
            auto fib_table_size = std::stoi(std::string(env_p));
            DOUT(std::cout << " DEBUG: set fib table size: " << fib_table_size << std::endl;);
            m_my_panini_fib.set_max_entry_count(fib_table_size);
        } catch (...) {
            DOUT(std::cout << " DEBUG: failed to parse fib table size" << std::endl;);
        }
    } else {
        DOUT(std::cout << " DEBUG: fib table size not given" << std::endl;);
    }

    //if (const char* env_p = std::getenv("FIB_TABLE_EXPIRE_TIME")) {
    //try {
    //auto fib_table_expire_time = std::stoi(std::string(env_p));
    //DOUT(std::cout << " DEBUG: set fib table expire time: " << fib_table_size << std::endl;);
    //m_my_panini_fib.set_default_expire_time(fib_table_expire_time);
    //} catch(...){
    //DOUT(std::cout << " DEBUG: failed to parse fib table expire time" << std::endl;);
    //}
    //} else {
    //DOUT(std::cout << " DEBUG: fib table expire time not given" << std::endl;);
    //}
}

void my_panini::afterReceiveInterest(const Face& inFace,
                                     const Interest& interest,
                                     shared_ptr<fib::Entry> fibEntry,
                                     shared_ptr<pit::Entry> pitEntry)
{
    DOUT(std::cout << "##-- DEBUG: PANINI Strategy (afterReceiveInterest start)" << std::endl;)

    //m_my_logger.log("panini", "afterReceiveInterest", interest.getName().toUri());

    auto is_upstream = [this](const Face & f, const std::string & name) {
        bool route_available;
        std::set<int> ref_faces;
        std::tie(route_available, ref_faces) = m_my_nac_fib.route_to_faces(name);

        if (!route_available) { //is empty ==> is alles egal nac gibt es nicht
            return false;
        } else if (*ref_faces.begin() == f.getId()) { //is gleich  ==> super
            return true;
        } else {
            return false; //is ungleich ==> not the upstream
        }
    };


    auto is_intern_face = [](const Face & f) {
        if (f.getLocalUri().toString().find("dev://") == std::string::npos) { //inface is a dev
            return true;
        } else { //inFace is a downstream
            return false;
        }
    };

    const fib::NextHopList& nexthops = fibEntry->getNextHops();

    std::string interest_name = interest.getName().toUri();

    if (m_my_panini_fib.has_prefix(interest_name, "/nac")) {//nac discover message ==> to set upstream and to set the exclude list for the fib
        DOUT(std::cout << "DEBUG: found nac message:" << interest_name << std::endl;)
        m_my_logger.log("panini", "afterRecvNac", interest_name);

        //## /nac/panini/nac0/+in/0
        std::string interest_type_prefix; //## /nac
        std::string nac_name; //## /panini/nac0
        std::string ex_in_postfix; //## /+in
        std::tie(interest_type_prefix, nac_name) = my_routing_tree::split_prefix(interest_name); //## /nac, /panini/nac0/+in/0
        std::tie(std::ignore, nac_name) = my_routing_tree::split_postfix(nac_name); //## /0, /panini/nac0/+in/0
        std::tie(ex_in_postfix, nac_name) = my_routing_tree::split_postfix(nac_name); //## /+in, /panini/nac0/

        //only on nac per nac name can be registered, and not for an internal face
        if (!std::get<0>(m_my_nac_fib.route_to_faces(nac_name))) {
            if (!is_intern_face(inFace)) {
                m_my_nac_fib.set_route(interest_name, inFace.getId());
                DOUT(std::cout << "DEBUG: set nac fib: " << interest_name << " to outFace: " << inFace.getLocalUri().toString() << " mit faceid: " << inFace.getId() << std::endl;);
                m_my_panini_fib.set_is_nac(false); //is only nac if it comes from an internal face
            } else {
                m_my_panini_fib.set_is_nac(true);
                DOUT(std::cout << "is nac: " << m_my_panini_fib.get_is_nac() << std::endl;)
            }
            DOUT(std::cout << "nac_fib: " << m_my_nac_fib << std::endl;);
        }

        //send nac message to all other faces and fill m_panini_fib exlcude face list
        for (fib::NextHopList::const_iterator it = nexthops.begin(); it != nexthops.end(); ++it) {
            shared_ptr<Face> outFace = it->getFace();

            m_my_panini_fib.add_available_face(outFace->getId());

            if (pitEntry->canForwardTo(*outFace) && outFace->getId() != inFace.getId()) { //send nac message not back to receiption face
                DOUT(std::cout << " DEBUG: send nac message to: " << outFace->getLocalUri().toString() << " faceid: " << outFace->getId() << std::endl;)
                this->sendInterest(pitEntry, outFace);
                m_my_logger.log("panini", "afterSendNac", interest_name);
            }
        }

    } else if (m_my_panini_fib.has_prefix(interest_name, "/nam_msg")) { //process nam_msg
        DOUT(std::cout << "DEBUG: found nam message:" << interest_name << std::endl;)
        m_my_logger.log("panini", "afterRecvNam", interest_name, m_my_panini_fib.get_entry_count());

        m_my_panini_fib.set_route(interest_name, inFace.getId());
        //DOUT(std::cout << "panini_fib: " << m_my_panini_fib << std::endl;);

        for (fib::NextHopList::const_iterator it = nexthops.begin(); it != nexthops.end(); ++it) {
            shared_ptr<Face> outFace = it->getFace();

            std::string nam_prefix, remainder;
            std::tie(nam_prefix, remainder) = my_routing_tree::split_prefix(interest_name);

            if (is_upstream(*outFace, remainder)) {
                if (pitEntry->canForwardTo(*outFace)) {
                    DOUT(std::cout << " DEBUG: found upstream to forward nam_msg: " << outFace->getLocalUri().toString() << " mit faceid: " << outFace->getId() << std::endl;)
                    this->sendInterest(pitEntry, outFace);
                    m_my_logger.log("panini", "afterSendNam", interest_name);
                    break;
                }
            }
        }
    } else if (m_my_panini_fib.has_prefix(interest_name, "/panini")) { //normal interest for request data
        DOUT(std::cout << "DEBUG: found interest message:" << interest_name << " on face: " << inFace.getLocalUri().toString() << " mit faceid: " << inFace.getId() << std::endl;)
        m_my_logger.log("panini", "afterRecvInterest", interest_name, m_my_panini_fib.get_entry_count());

        bool route_available;
        std::set<int> face_set;
        std::tie(route_available, face_set) = m_my_panini_fib.route_to_faces(interest_name);

        if (route_available) { //fib entry ????? die frage müsste lauten gibt es eine route
            DOUT(
                std::cout << " DEBUG: face_set not empty: ";
            for (auto x : face_set) {
            std::cout << x << " ";
        }
        std::cout << std::endl;
        );

            for (fib::NextHopList::const_iterator it = nexthops.begin(); it != nexthops.end(); ++it) {
                shared_ptr<Face> outFace = it->getFace();

                //if (pitEntry->canForwardTo(*outFace)) {
                    if (is_intern_face(*outFace)) {
                        this->sendInterest(pitEntry, outFace);
                        DOUT(std::cout << " DEBUG: (a) send Interest to outFace: " << outFace->getLocalUri().toString() << " mit faceid: " << outFace->getId() << std::endl;)
                        break;
                    } else if (face_set.find(outFace->getId()) != face_set.end()) {
                        this->sendInterest(pitEntry, outFace, true);
                        DOUT(std::cout << " DEBUG: (b) send Interest to outFace: " << outFace->getLocalUri().toString() << " mit faceid: " << outFace->getId() << std::endl;)
                        m_my_logger.log("panini", "afterSendUnicastInterest", interest_name);
                        break;
                    }
                //}

            }
        } else if (is_upstream(inFace, interest_name)) { //broadcast
            DOUT(std::cout << " DEBUG: inFace is the upstream: " << inFace.getId() << " mit faceid: " << inFace.getLocalUri().toString() << std::endl;)
            //for (fib::NextHopList::const_reverse_iterator it = nexthops.rbegin(); it != nexthops.rend(); ++it) {
            for (fib::NextHopList::const_iterator it = nexthops.begin(); it != nexthops.end(); ++it) {
                shared_ptr<Face> outFace = it->getFace();

                //if (pitEntry->canForwardTo(*outFace)) {
                    if (is_intern_face(*outFace)) {
                        this->sendInterest(pitEntry, outFace);
                        DOUT(std::cout << " DEBUG: (a) send Interest to outFace: " << outFace->getLocalUri().toString() << " mit faceid: " << outFace->getId() << std::endl;)
                        break;
                    } else if (!route_available && !is_upstream(*outFace, interest_name)) {
                        this->sendInterest(pitEntry, outFace);
                        DOUT(std::cout << " DEBUG: (b) send Interest to outFace: " << outFace->getLocalUri().toString() << " mit faceid: " << outFace->getId() << std::endl;)
                        m_my_logger.log("panini", "afterSendBroadcastInterest", interest_name);
                    //}

                }
            }
        } else { //forward to upstream
            DOUT(std::cout << " DEBUG: inFace is a downstream: " << inFace.getId() << " mit faceid: " << inFace.getLocalUri().toString() << std::endl;)

            //for (fib::NextHopList::const_reverse_iterator it = nexthops.rbegin(); it != nexthops.rend(); ++it) {
            for (fib::NextHopList::const_iterator it = nexthops.begin(); it != nexthops.end(); ++it) {
                shared_ptr<Face> outFace = it->getFace();

                if (pitEntry->canForwardTo(*outFace)) {
                    if (is_intern_face(*outFace) ) {
                        this->sendInterest(pitEntry, outFace);
                        DOUT(std::cout << " DEBUG: (a) send Interest to outFace: " << outFace->getLocalUri().toString() << " mit faceid: " << outFace->getId() << std::endl;)
                        break;
                    } else if (is_upstream(*outFace, interest_name)) {
                        this->sendInterest(pitEntry, outFace);
                        DOUT(std::cout << " DEBUG: (b) send Interest to outFace: " << outFace->getLocalUri().toString()  << " mit faceid: " << outFace->getId() << std::endl;)
                        m_my_logger.log("panini", "afterSendUnicastInterest", interest_name);
                        break;
                    }
                }
            }
        }
    }

    if (!pitEntry->hasUnexpiredOutRecords()) {
        this->rejectPendingInterest(pitEntry);
    }

    DOUT(std::cout << "DEBUG: PANINI Strategy (afterReceiveInterest stop)" << std::endl;)
}

void my_panini::beforeSatisfyInterest(shared_ptr<pit::Entry> pitEntry,
                                      const Face& inFace, const Data& data)
{
    //std::cout << "##-- DEBUG: PANINI Strategy (beforeSatisfyInterest start)" << std::endl;

    //m_my_logger.log("panini", "beforeSatisfyInterest", data.getName().toUri());

    //std::cout << "DEBUG: PANINI Strategy (beforeSatisfyInterest stop)" << std::endl;
}

} // namespace fw
} // namespace nfd
