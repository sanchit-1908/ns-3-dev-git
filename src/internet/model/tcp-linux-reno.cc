/*
 * Copyright (c) 2019 NITK Surathkal
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Apoorva Bhargava <apoorvabhargava13@gmail.com>
 *         Mohit P. Tahiliani <tahiliani@nitk.edu.in>
 *
 */

#include "tcp-linux-reno.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("TcpLinuxReno");
NS_OBJECT_ENSURE_REGISTERED(TcpLinuxReno);

TypeId
TcpLinuxReno::GetTypeId()
{
    static TypeId tid = TypeId("ns3::TcpLinuxReno")
                            .SetParent<TcpCongestionOps>()
                            .SetGroupName("Internet")
                            .AddConstructor<TcpLinuxReno>();
    return tid;
}

TcpLinuxReno::TcpLinuxReno()
    : TcpCongestionOps()
{
    NS_LOG_FUNCTION(this);
}

TcpLinuxReno::TcpLinuxReno(const TcpLinuxReno& sock)
    : TcpCongestionOps(sock)
{
    NS_LOG_FUNCTION(this);
}

TcpLinuxReno::~TcpLinuxReno()
{
}

uint32_t
TcpLinuxReno::SlowStart(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked);

    if (segmentsAcked >= 1)
    {
        uint32_t sndCwnd = tcb->m_cWnd;
        tcb->m_cWnd =
            std::min((sndCwnd + (segmentsAcked * tcb->m_segmentSize)), (uint32_t)tcb->m_ssThresh);
        NS_LOG_INFO("In SlowStart, updated to cwnd " << tcb->m_cWnd << " ssthresh "
                                                     << tcb->m_ssThresh);
        return segmentsAcked - ((tcb->m_cWnd - sndCwnd) / tcb->m_segmentSize);
    }

    return 0;
}

void
TcpLinuxReno::CongestionAvoidance(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked);

    uint32_t w = tcb->m_cWnd / tcb->m_segmentSize;

    // Floor w to 1 if w == 0
    if (w == 0)
    {
        w = 1;
    }

    NS_LOG_DEBUG("w in segments " << w << " m_cWndCnt " << m_cWndCnt << " segments acked "
                                  << segmentsAcked);
    if (m_cWndCnt >= w)
    {
        m_cWndCnt = 0;
        tcb->m_cWnd += tcb->m_segmentSize;
        NS_LOG_DEBUG("Adding 1 segment to m_cWnd");
    }

    m_cWndCnt += segmentsAcked;
    NS_LOG_DEBUG("Adding 1 segment to m_cWndCnt");
    if (m_cWndCnt >= w)
    {
        uint32_t delta = m_cWndCnt / w;

        m_cWndCnt -= delta * w;
        tcb->m_cWnd += delta * tcb->m_segmentSize;
        NS_LOG_DEBUG("Subtracting delta * w from m_cWndCnt " << delta * w);
    }
    NS_LOG_DEBUG("At end of CongestionAvoidance(), m_cWnd: " << tcb->m_cWnd
                                                             << " m_cWndCnt: " << m_cWndCnt);
}

void
TcpLinuxReno::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked);

    // Linux tcp_in_slow_start() condition
    if (tcb->m_cWnd < tcb->m_ssThresh)
    {
        NS_LOG_DEBUG("In slow start, m_cWnd " << tcb->m_cWnd << " m_ssThresh " << tcb->m_ssThresh);
        segmentsAcked = SlowStart(tcb, segmentsAcked);
    }
    else
    {
        NS_LOG_DEBUG("In cong. avoidance, m_cWnd " << tcb->m_cWnd << " m_ssThresh "
                                                   << tcb->m_ssThresh);
        CongestionAvoidance(tcb, segmentsAcked);
    }
}

std::string
TcpLinuxReno::GetName() const
{
    return "TcpLinuxReno";
}

uint32_t
TcpLinuxReno::GetSsThresh(Ptr<const TcpSocketState> state, uint32_t bytesInFlight)
{
    NS_LOG_FUNCTION(this << state << bytesInFlight);

    // In Linux, it is written as:  return max(tp->snd_cwnd >> 1U, 2U);
    return std::max<uint32_t>(2 * state->m_segmentSize, state->m_cWnd / 2);
}

TcpLinuxReno::EnterCwr(uint32_t currentDelivered)
{
    NS_LOG_FUNCTION(this << currentDelivered);
    m_tcb->m_ssThresh = m_congestionControl->GetSsThresh(m_tcb, BytesInFlight());
    m_tcb->m_ssThresh = (tcb->m_cWnd)*0.85;
    NS_LOG_DEBUG("Reduce ssThresh to " << m_tcb->m_ssThresh);
    // Do not update m_cWnd, under assumption that recovery process will
    // gradually bring it down to m_ssThresh.  Update the 'inflated' value of
    // cWnd used for tracing, however.
    m_tcb->m_cWndInfl = m_tcb->m_ssThresh;
    NS_ASSERT(m_tcb->m_congState != TcpSocketState::CA_CWR);
    NS_LOG_DEBUG(TcpSocketState::TcpCongStateName[m_tcb->m_congState] << " -> CA_CWR");
    m_tcb->m_congState = TcpSocketState::CA_CWR;
    // CWR state will be exited when the ack exceeds the m_recover variable.
    // Do not set m_recoverActive (which applies to a loss-based recovery)
    // m_recover corresponds to Linux tp->high_seq
    m_recover = m_tcb->m_highTxMark;
    if (!m_congestionControl->HasCongControl())
    {
        // If there is a recovery algorithm, invoke it.
        m_recoveryOps->EnterRecovery(m_tcb, m_dupAckCount, UnAckDataCount(), currentDelivered);
        NS_LOG_INFO("Enter CWR recovery mode; set cwnd to " << m_tcb->m_cWnd << ", ssthresh to "
                                                            << m_tcb->m_ssThresh << ", recover to "
                                                            << m_recover);
    }
}

Ptr<TcpCongestionOps>
TcpLinuxReno::Fork()
{
    return CopyObject<TcpLinuxReno>(this);
}

} // namespace ns3
