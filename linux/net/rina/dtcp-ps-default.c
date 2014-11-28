/*
 * Default policy set for DTCP
 *
 *    Vincenzo Maffione <v.maffione@nextworks.it>
 *    Francesco Salvestrini <f.salvestrini@nextworks.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/random.h>

#define RINA_PREFIX "dtcp-ps-default"

#include "logs.h"
#include "rds/rmem.h"
#include "dtcp-ps.h"
#include "dtp.h"
#include "dtcp.h"
#include "dtcp-utils.h"
#include "dt-utils.h"
#include "debug.h"

static int
default_sv_update(struct dtcp_ps * ps, seq_num_t seq)
{
        struct dtcp * dtcp = ps->dm;
        int                  retval = 0;
        struct dtcp_config * dtcp_cfg;

        bool                 flow_ctrl;
        bool                 win_based;
        bool                 rate_based;
        bool                 rtx_ctrl;
        seq_num_t            LWE;

        if (!dtcp) {
                LOG_ERR("No instance passed, cannot run policy");
                return -1;
        }

        dtcp_cfg = dtcp_config_get(dtcp);
        if (!dtcp_cfg)
                return -1;

        flow_ctrl  = ps->flow_ctrl;
        win_based  = dtcp_window_based_fctrl(dtcp_cfg);
        rate_based = dtcp_rate_based_fctrl(dtcp_cfg);
        rtx_ctrl   = ps->rtx_ctrl;

        if (flow_ctrl) {
                if (win_based) {
                        if (ps->rcvr_flow_control(ps, seq)) {
                                LOG_ERR("Failed Rcvr Flow Control policy");
                                retval = -1;
                        }
                }

                if (rate_based) {
                        LOG_DBG("Rate based fctrl invoked");
                        if (ps->rate_reduction(ps)) {
                                LOG_ERR("Failed Rate Reduction policy");
                                retval = -1;
                        }
                }

                if (!rtx_ctrl) {
                        LOG_DBG("Receiving flow ctrl invoked");
                        if (ps->receiving_flow_control(ps, seq)) {
                                LOG_ERR("Failed Receiving Flow Control "
                                        "policy");
                                retval = -1;
                        }

                        return retval;
                }
        }
        LWE = dt_sv_rcv_lft_win(dtcp_dt(dtcp));

        if (rtx_ctrl) {
                LOG_DBG("Retransmission ctrl invoked");
                if (ps->rcvr_ack(ps, seq)) {
                        LOG_ERR("Failed Rcvr Ack policy");
                        retval = -1;
                }
        }

        return retval;
}

static int
default_lost_control_pdu(struct dtcp_ps * ps)
{
        struct dtcp * dtcp = ps->dm;

        if (!dtcp) {
                LOG_ERR("No instance passed, cannot run policy");
                return -1;
        }

#if 0
        struct pdu * pdu_ctrl;
        seq_num_t last_rcv_ctrl, snd_lft, snd_rt;

        last_rcv_ctrl = last_rcv_ctrl_seq(dtcp);
        snd_lft       = snd_lft_win(dtcp);
        snd_rt        = snd_rt_wind_edge(dtcp);
        pdu_ctrl      = pdu_ctrl_ack_create(dtcp,
                                            last_rcv_ctrl,
                                            snd_lft,
                                            snd_rt);
        if (!pdu_ctrl) {
                LOG_ERR("Failed Lost Control PDU policy");
                return -1;
        }

        if (pdu_send(dtcp, pdu_ctrl))
                return -1;
#endif

        LOG_DBG("Default lost control pdu policy");

        return 0;
}

static int
default_rcvr_ack(struct dtcp_ps * ps, seq_num_t seq)
{
        struct dtcp * dtcp = ps->dm;
        struct pdu * pdu;
        struct pci * pci;
        struct dtcp_config * dtcp_cfg;
        seq_num_t    LWE;
        seq_num_t    snd_lft;
        seq_num_t    snd_rt;
        pdu_type_t   type;

        if (!dtcp) {
                LOG_ERR("No instance passed, cannot run policy");
                return -1;
        }

        dtcp_cfg = dtcp_config_get(dtcp);
        if (!dtcp_cfg)
                return -1;

        LWE  = dt_sv_rcv_lft_win(dtcp_dt(dtcp));
        type = pdu_ctrl_type_get(dtcp, seq);
        if (!type)
                return 0;

        pdu  = pdu_ctrl_create_ni(dtcp, type);
        if (!pdu)
                return -1;

        pci = pdu_pci_get_rw(pdu);
        if (!pci) {
                pdu_destroy(pdu);
                return -1;
        }

        /*
         * FIXME: Shouldn't we check if PDU_TYPE_ACK_AND_FC or
         * PDU_TYPE_NACK_AND_FC ?
         */
        if (ps->flow_ctrl) {
                if (dtcp_window_based_fctrl(dtcp_cfg)) {
                        snd_lft = snd_lft_win(dtcp);
                        snd_rt  = snd_rt_wind_edge(dtcp);

                        pci_control_new_left_wind_edge_set(pci, LWE);
                        pci_control_new_rt_wind_edge_set(pci,
                                                         rcvr_rt_wind_edge(dtcp));
                        pci_control_my_left_wind_edge_set(pci, snd_lft);
                        pci_control_my_rt_wind_edge_set(pci, snd_rt);
                }

                if (dtcp_rate_based_fctrl(dtcp_cfg)) {
                        LOG_MISSING;
                }
        }

        switch (pci_type(pci)) {
        case PDU_TYPE_ACK_AND_FC:
        case PDU_TYPE_ACK:
                if (pci_control_ack_seq_num_set(pci, LWE)) {
                        pdu_destroy(pdu);
                        return -1;
                }
                dump_we(dtcp,pci);
                if (pdu_send(dtcp, pdu))
                        return -1;

                return 0;
        case PDU_TYPE_NACK_AND_FC:
        case PDU_TYPE_NACK:
                if (pci_control_ack_seq_num_set(pci, LWE + 1)) {
                        pdu_destroy(pdu);
                        return -1;
                }
                if (pdu_send(dtcp, pdu))
                        return -1;

                return 0;
        default:
                LOG_ERR("A PDU type not considered here has been produced");
                break;
        }

        return -1;
}

static int
default_sender_ack(struct dtcp_ps * ps, seq_num_t seq_num)
{
        struct dtcp * dtcp = ps->dm;

        if (!dtcp) {
                LOG_ERR("No instance passed, cannot run policy");
                return -1;
        }

        if (ps->rtx_ctrl) {
                struct rtxq * q;

                q = dt_rtxq(dtcp_dt(dtcp));
                if (!q) {
                        LOG_ERR("Couldn't find the Retransmission queue");
                        return -1;
                }
                rtxq_ack(q, seq_num, dt_sv_tr(dtcp_dt(dtcp)));
        }

        return 0;
}

static int
default_sending_ack(struct dtcp_ps * ps, seq_num_t seq)
{
        struct dtcp * dtcp = ps->dm;
        struct pdu * pdu_ctrl;
        seq_num_t    last_rcv_ctrl, snd_lft, snd_rt;

        if (!dtcp) {
                LOG_ERR("No instance passed, cannot run policy");
                return -1;
        }

        last_rcv_ctrl = last_rcv_ctrl_seq(dtcp);
        snd_lft       = snd_lft_win(dtcp);
        snd_rt        = snd_rt_wind_edge(dtcp);
        pdu_ctrl      = pdu_ctrl_ack_create(dtcp,
                                            last_rcv_ctrl,
                                            snd_lft,
                                            snd_rt);
        if (!pdu_ctrl)
                return -1;

        if (pdu_send(dtcp, pdu_ctrl))
                return -1;

        return 0;
}

static int
default_receiving_flow_control(struct dtcp_ps * ps, seq_num_t seq)
{
        struct dtcp * dtcp = ps->dm;
        struct pdu * pdu;
        struct pci * pci;
        seq_num_t    snd_lft, snd_rt, LWE;

        if (!dtcp) {
                LOG_ERR("No instance passed, cannot run policy");
                return -1;
        }
        pdu = pdu_ctrl_create_ni(dtcp, PDU_TYPE_FC);
        if (!pdu)
                return -1;

        pci = pdu_pci_get_rw(pdu);
        if (!pci) {
                pdu_destroy(pdu);
                return -1;
        }

        snd_lft = snd_lft_win(dtcp);
        snd_rt  = snd_rt_wind_edge(dtcp);
        LWE     = dt_sv_rcv_lft_win(dtcp_dt(dtcp));

        pci_control_new_left_wind_edge_set(pci, LWE);
        pci_control_new_rt_wind_edge_set(pci, rcvr_rt_wind_edge(dtcp));
        pci_control_my_left_wind_edge_set(pci, snd_lft);
        pci_control_my_rt_wind_edge_set(pci, snd_rt);

        if (pdu_send(dtcp, pdu))
                return -1;

        return 0;
}

static int
default_flow_control_overrun(struct dtcp_ps * instance, struct pdu * pdu)
{
        pdu_destroy(pdu);

        return 0;
}

static int
default_rcvr_flow_control(struct dtcp_ps * ps, seq_num_t seq)
{
        struct dtcp * dtcp = ps->dm;
        seq_num_t LWE;

        if (!dtcp) {
                LOG_ERR("No instance passed, cannot run policy");
                return -1;
        }

        LWE = dt_sv_rcv_lft_win(dtcp_dt(dtcp));
        update_rt_wind_edge(dtcp);

        LOG_DBG("DTCP: %pK", dtcp);
        LOG_DBG("LWE: %u  RWE: %u", LWE, rcvr_rt_wind_edge(dtcp));

        return 0;
}

static int
default_rate_reduction(struct dtcp_ps * ps)
{
        struct dtcp * dtcp = ps->dm;

        if (!dtcp) {
                LOG_ERR("No instance passed, cannot run policy");
                return -1;
        }

        LOG_MISSING;

        return 0;
}

static int dtcp_ps_set_policy_set_param(struct ps_base * bps,
                                       const char    * name,
                                       const char    * value)
{
        struct dtcp_ps *ps = container_of(bps, struct dtcp_ps, base);

        (void) ps;

        if (!name) {
                LOG_ERR("Null parameter name");
                return -1;
        }

        if (!value) {
                LOG_ERR("Null parameter value");
                return -1;
        }

        LOG_ERR("No such parameter to set");

        return -1;
}

static struct ps_base *
dtcp_ps_default_create(struct rina_component * component)
{
        struct dtcp * dtcp = dtcp_from_component(component);
        struct dtcp_ps * ps = rkzalloc(sizeof(*ps), GFP_KERNEL);

        if (!ps) {
                return NULL;
        }

        ps->base.set_policy_set_param   = dtcp_ps_set_policy_set_param;
        ps->dm                          = dtcp;
        ps->priv                        = NULL;
        ps->flow_init                   = NULL;
        ps->sv_update                   = default_sv_update;
        ps->lost_control_pdu            = default_lost_control_pdu;
        ps->rtt_estimator               = NULL;
        ps->retransmission_timer_expiry = NULL;
        ps->received_retransmission     = NULL;
        ps->rcvr_ack                    = default_rcvr_ack;
        ps->sender_ack                  = default_sender_ack;
        ps->sending_ack                 = default_sending_ack;
        ps->receiving_ack_list          = NULL;
        ps->initial_rate                = NULL;
        ps->receiving_flow_control      = default_receiving_flow_control;
        ps->update_credit               = NULL;
        ps->flow_control_overrun        = default_flow_control_overrun;
        ps->reconcile_flow_conflict     = NULL;
        ps->rcvr_flow_control           = default_rcvr_flow_control;
        ps->rate_reduction              = default_rate_reduction;
        ps->rcvr_control_ack            = NULL;
        ps->no_rate_slow_down           = NULL;
        ps->no_override_default_peak    = NULL;

        return &ps->base;
}

static void dtcp_ps_default_destroy(struct ps_base * bps)
{
        struct dtcp_ps *ps = container_of(bps, struct dtcp_ps, base);

        if (bps) {
                rkfree(ps);
        }
}

struct ps_factory dtcp_factory = {
        .owner          = THIS_MODULE,
        .create  = dtcp_ps_default_create,
        .destroy = dtcp_ps_default_destroy,
};
