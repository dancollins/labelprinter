#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#include <windows.h>
#include <wingdi.h>
#include <winspool.h>

#define PAPER_NAME_SIZE (64)

struct paper_size
{
    char name[PAPER_NAME_SIZE];
    short size;
    POINT dimensions;
};

struct label
{
    BITMAPFILEHEADER *header;
    DWORD size;
};

static struct option long_options[] = {
    {"printer", required_argument, NULL, 'p'},
    {"paper-size", required_argument, NULL, 's'},
    {"help", 0, NULL, 'h'},
    {NULL, 0, NULL, 0}};

static void print_usage(void)
{
    fprintf(stderr, "Usage: labelprinter [options] [filename...]\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -p, --printer NAME     Specify the printer name (default: system default)\n");
    fprintf(stderr, "  -s, --paper-size SIZE  Specify the paper size (default: printer default)\n");
    fprintf(stderr, "  -h, --help             Display this help message and exit\n");
    fprintf(stderr, "Arguments:\n");
    fprintf(stderr, "  filename               File(s) to process\n");
}

/**
 * @brief Get the default printer name.
 *
 * @return The default printer name, or `NULL` on failure.
 */
static char *get_default_printer(void)
{
    char *printer_name = NULL;
    DWORD size = 0;

    GetDefaultPrinter(NULL, &size);
    if (size == 0)
    {
        fprintf(stderr, "Failed to get default printer name.\n");
        goto exit;
    }

    printer_name = (char *)malloc(size);
    if (printer_name == NULL)
    {
        fprintf(
            stderr, "Failed to allocate memory for default printer name.\n");
        goto exit;
    }

    if (!GetDefaultPrinter(printer_name, &size))
    {
        fprintf(stderr, "Failed to get default printer name.\n");
        goto exit;
    }

    return printer_name;

exit:
    if (printer_name != NULL)
        free(printer_name);

    return NULL;
}

/**
 * @brief Get the default paper size for the specified printer.
 *
 * @param printer The printer to request the default page size name from.
 * @return The default paper size, or `NULL` on failure.
 */
static char *get_default_paper_size_name(char *printer_name)
{
    HANDLE printer = INVALID_HANDLE_VALUE;
    char *paper_size = NULL;
    DWORD size = 0;

    if (!OpenPrinter(printer_name, &printer, NULL))
    {
        fprintf(stderr, "Failed to open printer %s.\n", printer_name);
        goto exit;
    }

    /* Get PRINTER_INFO_2, which has the DEVMODE structure we need. */
    GetPrinter(printer, 2, NULL, 0, &size);
    if (size == 0)
    {
        fprintf(stderr, "Failed to get printer info.\n");
        goto exit;
    }

    PRINTER_INFO_2 *printer_info = (PRINTER_INFO_2 *)malloc(size);
    if (printer_info == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for printer info.\n");
        goto exit;
    }

    if (!GetPrinter(printer, 2, (void *)printer_info, size, &size))
    {
        fprintf(stderr, "Failed to get printer info.\n");
        goto exit;
    }

    if (printer_info->pDevMode == NULL)
    {
        fprintf(stderr, "DEVMODE not found in printer info.\n");
        goto exit;
    }

    paper_size = strdup(printer_info->pDevMode->dmFormName);
    if (paper_size == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for paper size name.\n");
        goto exit;
    }

    if (!ClosePrinter(printer))
    {
        fprintf(stderr, "Failed to close printer.\n");
        goto exit;
    }

    return paper_size;

exit:
    if (printer != INVALID_HANDLE_VALUE)
        ClosePrinter(printer);

    if (printer_info != NULL)
        free(printer_info);

    return NULL;
}

/**
 * @brief Get the paper size for the named paper size.
 *
 * @param printer_name The name of the printer to request the details from.
 * @param paper_size_name The name of the paper size to get.
 * @return The paper size, or `NULL` on failure.
 */
static struct paper_size *get_paper_size(
    char *printer_name, char *paper_size_name)
{
    struct paper_size *paper_size = NULL;

    int paper_count = 0;
    short *sizes = NULL;
    POINT *dimensions = NULL;
    char *names = NULL;

    /* We need to get DM_PAPERS and DM_PAPERSIZE from the driver - and we
     * do a bit of a dance to get there. First we get the size of the
     * structure, and then we get the structure itself! Any failures, we
     * need to clean up before returning. */

    paper_count = DeviceCapabilities(
        printer_name, NULL, DC_PAPERS, NULL, NULL);
    if (paper_count <= 0)
    {
        fprintf(stderr, "Failed to get paper sizes.\n");
        goto exit;
    }

    sizes = (short *)malloc(paper_count * sizeof(short));
    if (sizes == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for paper sizes.\n");
        goto exit;
    }

    if (DeviceCapabilities(
            printer_name, NULL, DC_PAPERS, (char *)sizes, NULL) <= 0)
    {
        fprintf(stderr, "Failed to get paper sizes.\n");
        goto exit;
    }

    dimensions = (POINT *)malloc(paper_count * sizeof(POINT));
    if (dimensions == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for paper sizes.\n");
        goto exit;
    }

    if (DeviceCapabilities(
            printer_name, NULL, DC_PAPERSIZE, (char *)dimensions, NULL) <= 0)
    {
        fprintf(stderr, "Failed to get paper dimensions.\n");
        goto exit;
    }

    names = (char *)malloc(paper_count * PAPER_NAME_SIZE);
    if (names == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for paper names.\n");
        goto exit;
    }

    if (DeviceCapabilities(
            printer_name, NULL, DC_PAPERNAMES, (char *)names, NULL) <= 0)
    {
        fprintf(stderr, "Failed to get paper names.\n");
        goto exit;
    }

    /* The arrays all share the same indicies - so we will search by name
     * and then copy the details across. */
    for (int i = 0; i < paper_count; i++)
    {
        const char *name = names + (i * PAPER_NAME_SIZE);
        if (strncmp(name, paper_size_name, PAPER_NAME_SIZE) != 0)
            continue;

        /* We found it! */
        paper_size = (struct paper_size *)malloc(sizeof(struct paper_size));
        if (paper_size == NULL)
        {
            fprintf(
                stderr,
                "Failed to allocate memory for paper size structure.\n");

            goto exit;
        }

        snprintf(paper_size->name, PAPER_NAME_SIZE, "%s", name);
        paper_size->size = sizes[i];
        paper_size->dimensions = dimensions[i];
        break;
    }

    if (paper_size == NULL)
    {
        fprintf(stderr, "Failed to find matching paper size.\n");
        goto exit;
    }

exit:
    if (sizes != NULL)
        free(sizes);

    if (dimensions != NULL)
        free(dimensions);

    if (names != NULL)
        free(names);

    return paper_size;
}

static BOOL set_paper_size(
    char *printer_name, struct paper_size *paper_size)
{
    HANDLE printer = INVALID_HANDLE_VALUE;
    DEVMODE *devmode = NULL;
    int devmode_size = 0;

    if (!OpenPrinter(printer_name, &printer, NULL))
    {
        fprintf(stderr, "Failed to open printer %s.\n", printer_name);
        goto exit;
    }

    /* Retrieve the DEVMODE structure. */
    devmode_size = DocumentProperties(
        NULL, printer, printer_name, NULL, NULL, 0);

    if (devmode_size <= 0)
    {
        fprintf(stderr, "Failed to get printer properties size.\n");
        goto exit;
    }

    devmode = (DEVMODE *)malloc(devmode_size);
    if (devmode == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for printer properties.\n");
        goto exit;
    }

    if (DocumentProperties(
            NULL,
            printer,
            printer_name,
            devmode,
            NULL,
            DM_OUT_BUFFER) != IDOK)
    {
        fprintf(stderr, "Failed to get printer properties.\n");
        goto exit;
    }

    /* Set the paper size. */
    devmode->dmFields = DM_PAPERSIZE;
    devmode->dmPaperSize = paper_size->size;

    if (DocumentProperties(
            NULL,
            printer,
            printer_name,
            devmode,
            devmode,
            (DM_IN_BUFFER | DM_OUT_BUFFER)) != IDOK)
    {
        fprintf(stderr, "Failed to set paper size.\n");
        goto exit;
    }

    if (!ClosePrinter(printer))
    {
        fprintf(stderr, "Failed to close printer.\n");
        goto exit;
    }

    return TRUE;

exit:
    if (printer != INVALID_HANDLE_VALUE)
        ClosePrinter(printer);

    if (devmode != NULL)
        free(devmode);

    return FALSE;
}

static struct label *open_label(char *filename)
{
    struct label *label = NULL;
    HANDLE f;
    DWORD sizel, sizeh, bytes_read;
    void *file_data = NULL;
    BOOL success = FALSE;

    f = CreateFile(
        filename,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        NULL);

    if (f == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Failed to open: %s\n", filename);
        goto exit;
    }

    sizel = GetFileSize(f, &sizeh);
    if (sizeh > 0)
    {
        fprintf(stderr, "%s is too large to process.\n", filename);
        goto exit;
    }

    file_data = malloc(sizel);
    if (file_data == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for file data.\n");
        goto exit;
    }

    success = ReadFile(f, file_data, sizel, &bytes_read, NULL);
    if (!success || bytes_read != sizel)
    {
        fprintf(stderr, "Failed to read file.\n");
        goto exit;
    }

    CloseHandle(f);

    label = (struct label *)malloc(sizeof(struct label));
    if (label == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for label.\n");
        goto exit;
    }

    label->header = (BITMAPFILEHEADER *)file_data;
    label->size = sizel;

    return label;

exit:
    if (label != NULL)
        free(label);

    if (f != INVALID_HANDLE_VALUE)
        CloseHandle(f);

    if (file_data != NULL)
        free(file_data);

    return NULL;
}

static void close_label(struct label *label)
{
    assert(label != NULL);
    free(label);
}

static void print_label(
    char *printer_name,
    struct paper_size *paper_size,
    char *filename)
{
    printf(" 🏷️ %s\n", filename);
}

int main(int argc, char **argv)
{
    char *printer_name = NULL, *default_printer_name = NULL;
    char *paper_size_name = NULL, *default_paper_size_name = NULL;
    int opt;

    SetConsoleOutputCP(CP_UTF8);

    struct paper_size *paper_size = NULL;

    while ((opt = getopt_long(
                argc,
                argv,
                "p:s:h",
                long_options,
                NULL)) != -1)
    {
        switch (opt)
        {
        case 'p':
            printer_name = optarg;
            break;

        case 's':
            paper_size_name = optarg;
            break;

        case 'h':
            print_usage();
            exit(EXIT_SUCCESS);
            break;

        case '?':
            print_usage();
            exit(EXIT_FAILURE);
            break;
        }
    }

    int file_count = argc - optind;
    if (file_count <= 0)
    {
        fprintf(stderr, "No files to process!\n");
        print_usage();
        exit(EXIT_FAILURE);
    }

    /* Grab the printer */
    if (printer_name == NULL)
    {
        default_printer_name = get_default_printer();

        if (default_printer_name == NULL)
        {
            goto exit;
        }

        printer_name = default_printer_name;
    }

    /* Grab the paper size. */
    if (paper_size_name == NULL)
    {
        default_paper_size_name = get_default_paper_size_name(printer_name);

        if (default_paper_size_name == NULL)
        {
            goto exit;
        }

        paper_size_name = default_paper_size_name;
    }

    paper_size = get_paper_size(printer_name, paper_size_name);
    if (paper_size == NULL)
    {
        goto exit;
    }

    /* Set up the printer.*/
    if (!set_paper_size(printer_name, paper_size))
    {
        goto exit;
    }

    printf(" 🖨️ %s\n", printer_name);
    printf(" 📄 %s\n", paper_size_name);

    for (int i = 0; i < file_count; i++)
    {
        char *filename = argv[optind + i];
        print_label(printer_name, paper_size, filename);
    }

exit:
    if (default_printer_name != NULL)
        free(default_printer_name);

    if (default_paper_size_name != NULL)
        free(default_paper_size_name);

    return 0;
}
