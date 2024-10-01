#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/printk.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>

// to enable debug, uncomment:
#define DEBUG

/*
 * ACPI 2.0 chapter 13 SMBus 2.0 EC register model
 */

//#define ACPI_EC_SMB_PRTCL	0x00	/* protocol, PEC */
//#define ACPI_EC_SMB_STS		0x01	/* status */
//#define ACPI_EC_SMB_ADDR	0x02	/* address */
//#define ACPI_EC_SMB_CMD		0x03	/* command */
//#define ACPI_EC_SMB_DATA	0x04	/* 32 data registers */
//#define ACPI_EC_SMB_BCNT	0x24	/* number of data bytes */
//#define ACPI_EC_SMB_ALRM_A	0x25	/* alarm address */
//#define ACPI_EC_SMB_ALRM_D	0x26	/* 2 bytes alarm data */

#define ACPI_EC_SMB_STS_DONE	0x80
#define ACPI_EC_SMB_STS_ALRM	0x40
#define ACPI_EC_SMB_STS_RES	0x20
#define ACPI_EC_SMB_STS_STATUS	0x1f

#define ACPI_EC_SMB_STATUS_OK		0x00
#define ACPI_EC_SMB_STATUS_FAIL		0x07
#define ACPI_EC_SMB_STATUS_DNAK		0x10
#define ACPI_EC_SMB_STATUS_DERR		0x11
#define ACPI_EC_SMB_STATUS_CMD_DENY	0x12
#define ACPI_EC_SMB_STATUS_UNKNOWN	0x13
#define ACPI_EC_SMB_STATUS_ACC_DENY	0x17
#define ACPI_EC_SMB_STATUS_TIMEOUT	0x18
#define ACPI_EC_SMB_STATUS_NOTSUP	0x19
#define ACPI_EC_SMB_STATUS_BUSY		0x1A
#define ACPI_EC_SMB_STATUS_PEC		0x1F

#define ACPI_EC_SMB_PRTCL_WRITE			0x00
#define ACPI_EC_SMB_PRTCL_READ			0x01
#define ACPI_EC_SMB_PRTCL_QUICK			0x02
#define ACPI_EC_SMB_PRTCL_BYTE			0x04
#define ACPI_EC_SMB_PRTCL_BYTE_DATA		0x06
#define ACPI_EC_SMB_PRTCL_WORD_DATA		0x08
#define ACPI_EC_SMB_PRTCL_BLOCK_DATA		0x0a
#define ACPI_EC_SMB_PRTCL_PROC_CALL		0x0c
#define ACPI_EC_SMB_PRTCL_BLOCK_PROC_CALL	0x0d
#define ACPI_EC_SMB_PRTCL_I2C_BLOCK_DATA	0x4a
#define ACPI_EC_SMB_PRTCL_PEC			0x80


struct ec_smbus_esmc_data
{
    u8 ptrcl;
    u8 addr;
    u8 cmd;
    u8 bus_num;
    u8 bcnt;
    u8 data[32];
} __packed;

static int ec_smbus_prepare_call_data (unsigned short flags, char read_write, int size,
                                    union i2c_smbus_data *data, struct ec_smbus_esmc_data *parm, 
                                    bool *to_read)
{
    u8 pec = (flags & I2C_CLIENT_PEC) ? ACPI_EC_SMB_PRTCL_PEC : 0;
    int num;

    if (read_write == I2C_SMBUS_READ) {
		parm->ptrcl = ACPI_EC_SMB_PRTCL_READ;
	} else {
		parm->ptrcl = ACPI_EC_SMB_PRTCL_WRITE;
	}

    switch (size) {
    case I2C_SMBUS_QUICK:
        parm->ptrcl |= ACPI_EC_SMB_PRTCL_QUICK;
        break;
    case I2C_SMBUS_BYTE:
        parm->ptrcl |= ACPI_EC_SMB_PRTCL_BYTE;
        break;
    case I2C_SMBUS_BYTE_DATA:
        parm->ptrcl |= ACPI_EC_SMB_PRTCL_BYTE_DATA;
        break;
    case I2C_SMBUS_WORD_DATA:
        parm->ptrcl |= ACPI_EC_SMB_PRTCL_WORD_DATA | pec;
        break;
    case I2C_SMBUS_BLOCK_DATA:
        parm->ptrcl |= ACPI_EC_SMB_PRTCL_BLOCK_DATA | pec;
        break;
    case I2C_SMBUS_PROC_CALL:
        parm->ptrcl |= ACPI_EC_SMB_PRTCL_PROC_CALL | pec;
        break;
    case I2C_SMBUS_BLOCK_PROC_CALL:
        parm->ptrcl |= ACPI_EC_SMB_PRTCL_BLOCK_PROC_CALL | pec;
        break;
    default:
        pr_debug("Transfer - Unsupported transaction %d\n", size);
        return -ENOTSUPP;
        break;
    }

    if(read_write == I2C_SMBUS_WRITE)
    {
        switch (size) {
            case I2C_SMBUS_BYTE:
                parm->cmd = data->byte;
                break;
            case I2C_SMBUS_BYTE_DATA:
                parm->data[0] = data->byte;
                break;
            case I2C_SMBUS_WORD_DATA:
                parm->data[0] = data->word & 0xFF;
                parm->data[1] = data->word >> 8;
                break;
            case I2C_SMBUS_BLOCK_DATA:
                num = data->block[0];
                if(num > 32)
                    return -ENOTSUPP;
                memcpy(parm->data, data->block+1, num*sizeof(parm->data[0]));
                parm->bcnt = num;
                break;
        }
    }
    else// if(read_write == I2C_SMBUS_READ)
    {
        *to_read = true;
    }
    switch(size)
    {
        case I2C_SMBUS_PROC_CALL:
            parm->data[0] = data->word & 0xFF;
            parm->data[1] = data->word >> 8;
            *to_read = true;
            break;
        case I2C_SMBUS_BLOCK_PROC_CALL:
            num = data->block[0];
            if(num > 32)
                return -ENOTSUPP;
            memcpy(parm->data, data->block+1, num*sizeof(parm->data[0]));
            parm->bcnt = num;
            *to_read = true;
            break;
    }
    return 0;
}

static int ec_smbus_parse_read (char read_write, int size, union i2c_smbus_data *data, 
                                    struct ec_smbus_esmc_data *parm)
{
    if(read_write == I2C_SMBUS_READ)
    {
        switch (size) {
            case I2C_SMBUS_BYTE:
            case I2C_SMBUS_BYTE_DATA:
                data->byte = parm->data[0];
                break;
            case I2C_SMBUS_WORD_DATA:
                data->word = parm->data[1];
                data->word = (data->word<<8) | parm->data[0];
                break;
            case I2C_SMBUS_BLOCK_DATA:
                if(parm->bcnt > 32)
                    return -ENOTSUPP;
                data->block[0] = parm->bcnt;
                memcpy(data->block+1, parm->data, data->block[0]*sizeof(parm->data[0]));
                break;
        }
    }
    switch(size)
    {
        case I2C_SMBUS_PROC_CALL:
            data->word = parm->data[1];
            data->word = (data->word<<8) | parm->data[0];
            break;
        case I2C_SMBUS_BLOCK_PROC_CALL:
            if(parm->bcnt > 32)
                return -ENOTSUPP;
            data->block[0] = parm->bcnt;
            memcpy(data->block+1, parm->data, data->block[0]*sizeof(parm->data[0]));
            break;
    }
    return 0;
}

struct ec_smbus_acpi_names
{
    const char *smb_sts, *smb_prtcl, *smb_addr, *smb_cmd, *smb_data, *smb_bcnt, 
               *smb_alrm_addr, *smb_alrm_data,
               *smb_busnum,  // acer extention, not in acpi (maybe it is bus number?)
               *smb_mutex;   // optional
    int bus_num;
    int flags;
#define EC_SMBUS_FLAG_ALRM_ADDR_DATA_MERGED     BIT(0)
} acer_acpi_names = {
    .smb_sts        = "\\_SB_.PCI0.LPC0.EC0_.SMST",
    .smb_prtcl      = "\\_SB_.PCI0.LPC0.EC0_.SMPR",
    .smb_addr       = "\\_SB_.PCI0.LPC0.EC0_.SMAD",
    .smb_cmd        = "\\_SB_.PCI0.LPC0.EC0_.SMCM",
    .smb_data       = "\\_SB_.PCI0.LPC0.EC0_.SMD0",
    .smb_bcnt       = "\\_SB_.PCI0.LPC0.EC0_.BCNT",
    .smb_alrm_addr  = "\\_SB_.PCI0.LPC0.EC0_.SMAA",
    .smb_alrm_data  = NULL,
    .smb_busnum     = "\\_SB_.PCI0.LPC0.EC0_.SMBN",
    .smb_mutex      = "\\_SB_.PCI0.LPC0.EC0_.ESCX",
    .bus_num = 2,
    .flags = EC_SMBUS_FLAG_ALRM_ADDR_DATA_MERGED
}; 


// not sure if bus_num is really bus num ...
// works pretty good with zero
static int ec_smbus_call_esmc (struct ec_smbus_esmc_data *parm, bool read_data)
{
    int err;
    BUG_ON(parm->bcnt>32);
    union acpi_object arg_buf;
    arg_buf.type = ACPI_TYPE_BUFFER;
    arg_buf.buffer.pointer = (u8*)parm;
    arg_buf.buffer.length = 5 + parm->bcnt;

    acpi_status status;
    struct acpi_object_list args;
    args.pointer = &arg_buf;
    args.count = 1;

    status = acpi_evaluate_object(NULL, "\\_SB_.PCI0.LPC0.EC0_.ESMC", &args, NULL);
    if(ACPI_FAILURE(status))
    {
        pr_err("Cannot call EC0.ESMC, error: %s\n", acpi_format_exception(status));
        return -1;
    }
    
    // this is a race condition mess, but acer ...

    u8 res_buffer[32+sizeof(union acpi_object)]; 
    u8 res_sts, res_size;
    struct acpi_buffer smb_res_buf;
    union acpi_object *smb_res_obj = (union acpi_object*)res_buffer;

    smb_res_buf.length=sizeof(res_buffer);
    smb_res_buf.pointer=res_buffer;
    status = acpi_evaluate_object_typed(NULL, "\\_SB_.PCI0.LPC0.EC0_.SMST", NULL, &smb_res_buf, ACPI_TYPE_INTEGER);
    if(ACPI_FAILURE(status) || smb_res_obj->integer.value>255)
    {
        pr_err("Cannot call EC0_.SMST, error: %s\n", acpi_format_exception(status));
        pr_err("Status is %llu\n", smb_res_obj->integer.value);
        return -1;
    }
    res_sts = (u8)smb_res_obj->integer.value;

    smb_res_buf.length=sizeof(res_buffer);
    smb_res_buf.pointer=res_buffer;
    status = acpi_evaluate_object_typed(NULL, "\\ESMS", NULL, &smb_res_buf, ACPI_TYPE_INTEGER);
    if(ACPI_FAILURE(status) || smb_res_obj->integer.value>255)
    {
        pr_err("Cannot call ESMS, error: %s\n", acpi_format_exception(status));
        return -1;
    }
    res_size = (u8)smb_res_obj->integer.value;

    if(res_size <= 32)
    {
        // read success
        if(read_data)
        {
            parm->bcnt = res_size;

            smb_res_buf.length=sizeof(res_buffer);
            smb_res_buf.pointer=res_buffer;
            status = acpi_evaluate_object_typed(NULL, "\\ESMB", NULL, &smb_res_buf, ACPI_TYPE_BUFFER);
            if(ACPI_FAILURE(status))
            {
                pr_err("Cannot call ESMB, error: %s\n", acpi_format_exception(status));
                return -1;
            }

            memcpy(parm->data, smb_res_obj->buffer.pointer, res_size*sizeof(parm->data[0]));
        }
        pr_devel("I2C transaction success!\n");
        return 0;
    }
    else
    {
        // read fail
        pr_debug("I2C transaction failed with code: %x\n", (res_sts&0x1F));
        switch(res_sts&0x1F)
        {
            case 0x00:
                err = -EAGAIN; // no error?!
                break;
            case 0x10:
                err = -ENXIO;
                break;
            case 0x18:
                err = -ETIMEDOUT;
                break;
            case 0x19:
                err = -EOPNOTSUPP;
                break;
            case 0x1A:
                err = -EAGAIN;
                break;
            case 0x1F:
                err = -EBADMSG;
                break;
            default:
                err = -EIO;
                break;
        }
        return err;
    }
}

static int ec_smbus_xfer(struct i2c_adapter *adap, u16 addr,
                   unsigned short flags, char read_write,
                   u8 command, int size, union i2c_smbus_data *data)
{
    // const struct ec_smbus_acpi_names *names = i2c_get_adapdata(adap);
    int err;
    struct ec_smbus_esmc_data call_data = {
        .cmd = command,
        .addr = addr << 1,
        .bus_num = 0, // TODO: bus 0
    };
    bool read_data;

    err = ec_smbus_prepare_call_data(flags, read_write, size, data, &call_data, &read_data);
    if(err < 0)
    {
        return err;
    }
    
    err = ec_smbus_call_esmc(&call_data, read_data);
    if(err < 0)
    {
        return err;
    }

    if(read_data)
    {
        err = ec_smbus_parse_read(read_write, size, data, &call_data);
        if(err < 0)
        {
            return err;
        }
    }

    return 0;
}

static u32 ec_smbus_func(struct i2c_adapter *adapter)
{
	return (I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
		I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
		I2C_FUNC_SMBUS_BLOCK_DATA |
		I2C_FUNC_SMBUS_PROC_CALL |
		I2C_FUNC_SMBUS_BLOCK_PROC_CALL |
		I2C_FUNC_SMBUS_PEC);
}

static struct i2c_algorithm ec_smbus_algo = {
    .smbus_xfer = ec_smbus_xfer,
    .functionality = ec_smbus_func
};

static int ec_smbus_probe (struct platform_device *pdev)
{
    // TODO: ACPI probe
    
    int err;
    struct i2c_adapter *adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
    if(!adapter)
    {
        pr_err("No memory to allocate adapter\n");
        err = -ENOMEM;
        goto error_adapter_allocate;
    }
    strscpy(adapter->name, pdev->name, sizeof(adapter->name));
    adapter->owner = THIS_MODULE;
    adapter->dev.parent = &pdev->dev;
    adapter->algo = &ec_smbus_algo;
    
    err = i2c_add_adapter(adapter);
    if(err)
    {
        pr_err("Cannot add adapter\n");
        goto error_adapter_add;
    }

    platform_set_drvdata(pdev, adapter);

    return 0;

error_adapter_add:
    kfree(adapter);

error_adapter_allocate:
    return err;
}

static int ec_smbus_remove (struct platform_device *pdev)
{
    struct i2c_adapter *adapter = platform_get_drvdata(pdev);

    i2c_del_adapter(adapter);

    kfree(adapter);

    return 0;
}

static struct platform_driver ec_smbus_driver = {
    .driver = {
        .name = "acer-ec-smbus"
    },
    .probe = ec_smbus_probe,
    .remove = ec_smbus_remove
};

static struct platform_device *ec_smbus_device;

static int __init init_ec_smbus(void)
{
	printk(KERN_INFO "Hello world 1.\n");

    pr_devel("Info test\n");

    int err;
    err = platform_driver_register(&ec_smbus_driver);
    if(err)
    {
        pr_err("Unable to register platform driver\n");
        goto error_platform_register;
    }

    ec_smbus_device = platform_device_register_simple("acer-ec-smbus", PLATFORM_DEVID_NONE, NULL, 0);
    if(!ec_smbus_device)
    {
        pr_err("Unable to allocate platform_device\n");
        err = -ENOMEM;
        goto error_deivce_reg_simpl;
    }
    if(IS_ERR(ec_smbus_device))
    {
        pr_err("Unable to allocate or register platform device\n");
        err = PTR_ERR(ec_smbus_device);
        goto error_deivce_reg_simpl;
    }


	return 0;

error_deivce_reg_simpl:
    platform_driver_unregister(&ec_smbus_driver);

error_platform_register:
    return err;
}

static void __exit unload_ec_smbus(void)
{
	printk(KERN_INFO "Goodbye world 1.\n");

    platform_device_unregister(ec_smbus_device);
    platform_driver_unregister(&ec_smbus_driver);

    return;
}

module_init(init_ec_smbus);
module_exit(unload_ec_smbus);


MODULE_LICENSE("GPL");
