/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2014-2015,  Regents of the University of California,
 *                           Arizona Board of Regents,
 *                           Colorado State University,
 *                           University Pierre & Marie Curie, Sorbonne University,
 *                           Washington University in St. Louis,
 *                           Beijing Institute of Technology,
 *                           The University of Memphis.
 *
 * This file is part of NFD (Named Data Networking Forwarding Daemon).
 * See AUTHORS.md for complete list of NFD authors and contributors.
 *
 * NFD is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * NFD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * NFD, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "broadcast-strategy.hpp"


namespace nfd {
namespace fw {

const Name BroadcastStrategy::STRATEGY_NAME("ndn:/localhost/nfd/strategy/broadcast/%FD%01");
NFD_REGISTER_STRATEGY(BroadcastStrategy);

BroadcastStrategy::BroadcastStrategy(Forwarder& forwarder, const Name& name)
  : Strategy(forwarder, name)
  , m_my_logger()
{
}

BroadcastStrategy::~BroadcastStrategy()
{
}

void
BroadcastStrategy::afterReceiveInterest(const Face& inFace,
                   const Interest& interest,
                   shared_ptr<fib::Entry> fibEntry,
                   shared_ptr<pit::Entry> pitEntry)
{
    m_my_logger.log("broadcast", "afterReceiveInterest", interest.getName().toUri());

  const fib::NextHopList& nexthops = fibEntry->getNextHops();

  for (fib::NextHopList::const_iterator it = nexthops.begin(); it != nexthops.end(); ++it) {
    shared_ptr<Face> outFace = it->getFace();
    if (pitEntry->canForwardTo(*outFace)) {
      this->sendInterest(pitEntry, outFace);
    }
  }

  if (!pitEntry->hasUnexpiredOutRecords()) {
    this->rejectPendingInterest(pitEntry);
  }
}

void BroadcastStrategy::beforeSatisfyInterest(shared_ptr<pit::Entry> pitEntry,
                                       const Face& inFace, const Data& data)
{
    m_my_logger.log("broadcast", "beforeSatisfyInterest", data.getName().toUri());
}

} // namespace fw
} // namespace nfd
