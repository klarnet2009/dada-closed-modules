# g4s r1: stock csr_fill_cmd_timeout (lookup table in the stock) has 12000 ms for WLAN_SER_CMD_VDEV_START_BSS and 17000 ms for WLAN_SER_CMD_VDEV_STOP_BSS (with PMO_RESUME_TIMEOUT 4000: SME_CMD_VDEV_START_BSS_TIMEOUT, SME_CMD_STOP_BSS_CMD_TIMEOUT); all other entries match the existing switch
@@@ core/sme/src/csr/csr_api_roam.c
<<<<
		cmd->cmd_timeout_duration = SME_CMD_POLICY_MGR_CMD_TIMEOUT;
		break;
	default:
		cmd->cmd_timeout_duration = SME_ACTIVE_LIST_CMD_TIMEOUT_VALUE;
====
		cmd->cmd_timeout_duration = SME_CMD_POLICY_MGR_CMD_TIMEOUT;
		break;
	case WLAN_SER_CMD_VDEV_START_BSS:
		cmd->cmd_timeout_duration = SME_CMD_VDEV_START_BSS_TIMEOUT;
		break;
	case WLAN_SER_CMD_VDEV_STOP_BSS:
		cmd->cmd_timeout_duration = SME_CMD_STOP_BSS_CMD_TIMEOUT;
		break;
	default:
		cmd->cmd_timeout_duration = SME_ACTIVE_LIST_CMD_TIMEOUT_VALUE;
>>>>
