cmd_/home/noah/workspace/iMX6ULL/04_Module_Commun/Module.symvers := sed 's/\.ko$$/\.o/' /home/noah/workspace/iMX6ULL/04_Module_Commun/modules.order | scripts/mod/modpost -m -a  -o /home/noah/workspace/iMX6ULL/04_Module_Commun/Module.symvers -e -i Module.symvers   -T -
