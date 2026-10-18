"""annab.py <reg> [shift]: annotate a lst2 listing (stdin) with struct connector_antiburn member names"""
import re,sys
F=[(0,'dev'),(8,'tzd_conn'),(0x10,'tzd_conn2'),(0x18,'thermal_board_nb'),(0x30,'debug_nb'),(0x48,'connect_nb'),(0x60,'hw_nb'),(0x78,'triggered'),(0x7c,'is_reset_vsafe0V'),(0x80,'ntc_alarm'),(0x84,'temperature[0]'),(0x88,'temperature[1]'),(0x8c,'temp_increase_rate[0]'),(0x90,'temp_increase_rate[1]'),(0x94,'fake_temp[0]'),(0x98,'fake_temp[1]'),(0x9c,'thermal_board_temp'),(0xa0,'max_thermal_board_temp'),(0xa4,'comb_sensorboard_con_trigger_temp'),(0xa8,'comb_rate_conn_trigger_temp'),(0xac,'trigger_temp'),(0xb0,'recharge_temp'),(0xb4,'mos_ctrl_gpio'),(0xb8,'otg_detect_en'),(0xbc,'support_soft_antiburn'),(0xc0,'support_hw_antiburn'),(0xc4,'use_double_ntc'),(0xc8,'monitor_interval'),(0xcc,'max_temp_increase_rate'),(0xd0,'thermal_zone_name'),(0xd8,'thermal_zone_name2'),(0xe0,'otg_plugin_status'),(0xe4,'usb_online'),(0xe8,'cid_status'),(0xec,'otg_boost_src'),(0xf0,'en_src'),(0xf4,'support_base_flip'),(0xf8,'disable_antiburn'),(0xfc,'support_elaboration'),(0x100,'anti_wake_lock'),(0x108,'monitor_work'),(0x190,'END')]
reg=sys.argv[1]; sh=int(sys.argv[2],0) if len(sys.argv)>2 else 0
def look(o):
    b=None
    for off,n in F:
        if off<=o: b=(off,n)
    return b[1] if b[0]==o else '%s+%d'%(b[1],o-b[0])
for l in sys.stdin:
    l=l.rstrip('\n')
    m=re.search(r'\[%s, #(-?0x[0-9a-f]+)\]'%reg,l) or re.search(r'\[%s\]'%reg,l)
    if m:
        o=(int(m.group(1),16) if m.groups() else 0)+sh
        l+='   ; '+look(o)
    else:
        m=re.search(r'(add|sub) x\d+, %s, #(0x[0-9a-f]+)'%reg,l)
        if m:
            o=(int(m.group(2),16)*(1 if m.group(1)=='add' else -1))+sh
            l+='   ; &'+look(o)
    print(l)
