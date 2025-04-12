#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#include <windows.h>
#include <wingdi.h>
#include <winspool.h>

#define PAPER_NAME_SIZE (64)

#define RESET "\x1b[0m"
#define DIM "\x1b[2m"
#define RED "\x1b[31m"

#define DBG(...)                          \
    {                                     \
        if (verbose)                      \
        {                                 \
            fputs(DIM "    ", stdout);    \
            fprintf(stdout, __VA_ARGS__); \
            fputs(RESET, stdout);         \
        }                                 \
    }

#define ERR(...)                      \
    {                                 \
        fputs(RED " 🚨 ", stderr);    \
        fprintf(stderr, __VA_ARGS__); \
        fputs(RESET, stderr);         \
    }

struct paper_size
{
    char name[PAPER_NAME_SIZE];
    short size;
    float width_mm;
    float height_mm;
};

struct label
{
    BITMAPFILEHEADER *header;
    BITMAPINFOHEADER *info_header;
    void *bits;
    int width, height;
    int xres, yres;
};

static struct option long_options[] = {
    {"printer", required_argument, NULL, 'p'},
    {"paper-size", required_argument, NULL, 's'},
    {"orientation", required_argument, NULL, 'o'},
    {"help", 0, NULL, 'h'},
    {NULL, 0, NULL, 0}};

static BOOL verbose = FALSE;
static BOOL dry_run = FALSE;

static void print_usage(void)
{
    fprintf(stderr, "Usage: labelprinter [options] [filename...]\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -p, --printer NAME                      Specify the printer name (default: system default)\n");
    fprintf(stderr, "  -s, --paper-size SIZE                   Specify the paper size (default: printer default)\n");
    fprintf(stderr, "  -o, --orientation [landscape|portrait]  Specify the orientation (default: printer default)\n");
    fprintf(stderr, "  -d, --dry-run                           Do not print, just simulate the operation\n");
    fprintf(stderr, "  -v, --verbose                           Enable verbose output\n");
    fprintf(stderr, "  -h, --help                              Display this help message and exit\n");
    fprintf(stderr, "Arguments:\n");
    fprintf(stderr, "  filename                                File(s) to process\n");
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
        ERR("Failed to get default printer name.\n");
        goto exit;
    }

    printer_name = (char *)malloc(size);
    if (printer_name == NULL)
    {
        ERR("Failed to allocate memory for default printer name.\n");
        goto exit;
    }

    if (!GetDefaultPrinter(printer_name, &size))
    {
        ERR("Failed to get default printer name.\n");
        goto exit;
    }

    DBG("Default printer: %s\n", printer_name);

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
        ERR("Failed to open printer %s.\n", printer_name);
        goto exit;
    }

    /* Get PRINTER_INFO_2, which has the DEVMODE structure we need. */
    GetPrinter(printer, 2, NULL, 0, &size);
    if (size == 0)
    {
        ERR("Failed to get printer info.\n");
        goto exit;
    }

    PRINTER_INFO_2 *printer_info = (PRINTER_INFO_2 *)malloc(size);
    if (printer_info == NULL)
    {
        ERR("Failed to allocate memory for printer info.\n");
        goto exit;
    }

    if (!GetPrinter(printer, 2, (void *)printer_info, size, &size))
    {
        ERR("Failed to get printer info.\n");
        goto exit;
    }

    if (printer_info->pDevMode == NULL)
    {
        ERR("DEVMODE not found in printer info.\n");
        goto exit;
    }

    paper_size = strdup(printer_info->pDevMode->dmFormName);
    if (paper_size == NULL)
    {
        ERR("Failed to allocate memory for paper size name.\n");
        goto exit;
    }

    if (!ClosePrinter(printer))
    {
        ERR("Failed to close printer.\n");
        goto exit;
    }

    DBG("Default paper size: %s\n", paper_size);

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
        ERR("Failed to get paper sizes.\n");
        goto exit;
    }

    DBG("Paper count: %d\n", paper_count);

    sizes = (short *)malloc(paper_count * sizeof(short));
    if (sizes == NULL)
    {
        ERR("Failed to allocate memory for paper sizes.\n");
        goto exit;
    }

    if (DeviceCapabilities(
            printer_name, NULL, DC_PAPERS, (char *)sizes, NULL) <= 0)
    {
        ERR("Failed to get paper sizes.\n");
        goto exit;
    }

    dimensions = (POINT *)malloc(paper_count * sizeof(POINT));
    if (dimensions == NULL)
    {
        ERR("Failed to allocate memory for paper sizes.\n");
        goto exit;
    }

    if (DeviceCapabilities(
            printer_name, NULL, DC_PAPERSIZE, (char *)dimensions, NULL) <= 0)
    {
        ERR("Failed to get paper dimensions.\n");
        goto exit;
    }

    names = (char *)malloc(paper_count * PAPER_NAME_SIZE);
    if (names == NULL)
    {
        ERR("Failed to allocate memory for paper names.\n");
        goto exit;
    }

    if (DeviceCapabilities(
            printer_name, NULL, DC_PAPERNAMES, (char *)names, NULL) <= 0)
    {
        ERR("Failed to get paper names.\n");
        goto exit;
    }

    /* The arrays all share the same indicies - so we will search by name
     * and then copy the details across. */
    for (int i = 0; i < paper_count; i++)
    {
        const char *name = names + (i * PAPER_NAME_SIZE);
        DBG("Checking paper size: %s\n", name);
        if (strncmp(name, paper_size_name, PAPER_NAME_SIZE) != 0)
            continue;

        /* We found it! */
        paper_size = (struct paper_size *)malloc(sizeof(struct paper_size));
        if (paper_size == NULL)
        {
            ERR("Failed to allocate memory for paper size structure.\n");
            goto exit;
        }

        snprintf(paper_size->name, PAPER_NAME_SIZE, "%s", name);
        paper_size->size = sizes[i];
        paper_size->width_mm = dimensions[i].x / 10.0f;
        paper_size->height_mm = dimensions[i].y / 10.0f;
        break;
    }

    if (paper_size == NULL)
    {
        ERR("Failed to find matching paper size.\n");
        goto exit;
    }

    DBG("Found paper size: name=%s, size=%d, width=%.1f mm, height=%.1f mm\n",
        paper_size->name,
        paper_size->size,
        paper_size->width_mm,
        paper_size->height_mm);

exit:
    if (sizes != NULL)
        free(sizes);

    if (dimensions != NULL)
        free(dimensions);

    if (names != NULL)
        free(names);

    return paper_size;
}

static DEVMODE *set_paper_size(
    char *printer_name,
    struct paper_size *paper_size,
    BOOL landscape)
{
    DEVMODE *devmode = NULL;
    HANDLE printer = INVALID_HANDLE_VALUE;
    int devmode_size = 0;

    DBG("Setting paper size to %s\n", paper_size->name, paper_size->size);

    if (!OpenPrinter(printer_name, &printer, NULL))
    {
        ERR("Failed to open printer %s.\n", printer_name);
        goto exit;
    }

    devmode_size = DocumentProperties(
        NULL, printer, (char *)printer_name, NULL, NULL, 0);

    if (devmode_size <= 0)
    {
        ERR("Failed to get printer properties size.\n");
        goto exit;
    }

    devmode = (DEVMODE *)malloc(devmode_size);
    if (devmode == NULL)
    {
        ERR("Failed to allocate memory for printer properties.\n");
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
        ERR("Failed to get printer properties.\n");
        goto exit;
    }

    /* Configure our page settings. */
    devmode->dmFields |= DM_PAPERSIZE | DM_ORIENTATION;
    devmode->dmPaperSize = paper_size->size;
    devmode->dmOrientation = landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;

    if (!dry_run)
    {
        if (DocumentProperties(
                NULL,
                printer,
                printer_name,
                devmode,
                devmode,
                (DM_IN_BUFFER | DM_OUT_BUFFER)) != IDOK ||
            devmode == NULL)
        {
            ERR("Failed to set paper size or devmode is invalid.\n");
            goto exit;
        }
    }

    DBG("Printer properties: paper size=%d, orientation=%s\n",
        devmode->dmPaperSize,
        (devmode->dmOrientation == DMORIENT_LANDSCAPE) ? "landscape" : "portrait");

    return devmode;

exit:
    if (printer != INVALID_HANDLE_VALUE)
        ClosePrinter(printer);

    if (devmode != NULL)
    {
        free(devmode);
    }

    return NULL;
}

static struct label *open_label(char *filename)
{
    HANDLE f;
    DWORD sizel, sizeh, bytes_read;
    void *file_data = NULL;
    BOOL success = FALSE;
    BITMAPFILEHEADER *header = NULL;
    struct label *label = NULL;

    if (filename == NULL)
    {
        ERR("Filename is NULL.\n");
        return NULL;
    }

    DBG("Opening label file: %s\n", filename);

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
        goto exit;
    }

    sizel = GetFileSize(f, &sizeh);
    if (sizeh > 0)
    {
        ERR("%s is too large to process.\n", filename);
        goto exit;
    }

    file_data = malloc(sizel);
    if (file_data == NULL)
    {
        ERR("Failed to allocate memory for file data.\n");
        goto exit;
    }

    success = ReadFile(f, file_data, sizel, &bytes_read, NULL);
    if (!success || bytes_read != sizel)
    {
        goto exit;
    }

    DBG("Read %d bytes\n", bytes_read);

    header = (BITMAPFILEHEADER *)file_data;
    DBG("Bitmap type: %x\n", header->bfType);
    DBG("Bitmap size: %d\n", header->bfSize);

    /* Ensure we're dealing with a bitmap file. */
    if ((header->bfType != 0x4D42) ||
        (header->bfSize != sizel))
    {
        ERR("%s is not a valid bitmap file.\n", filename);
        goto exit;
    }

    /* Ensure the file is well-formed. */
    if (header->bfOffBits < (sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) ||
        header->bfOffBits > sizel)
    {
        ERR("%s is not a valid bitmap file.\n", filename);
        goto exit;
    }

    /* Create our label structure. */
    label = (struct label *)malloc(sizeof(struct label));
    if (label == NULL)
    {
        ERR("Failed to allocate memory for label structure.\n");
        goto exit;
    }

    label->header = header;
    label->info_header = (BITMAPINFOHEADER *)(header + 1);
    label->bits = (char *)header + header->bfOffBits;
    label->width = label->info_header->biWidth;
    label->height = label->info_header->biHeight;
    label->xres = label->info_header->biXPelsPerMeter;
    label->yres = label->info_header->biYPelsPerMeter;

    DBG("Bitmap width: %d px\n", label->width);
    DBG("Bitmap height: %d px\n", label->height);
    DBG("Bitmap xres: %d px/m\n", label->xres);
    DBG("Bitmap yres: %d px/m\n", label->yres);

    CloseHandle(f);
    f = INVALID_HANDLE_VALUE;

    return label;

exit:
    if (f != INVALID_HANDLE_VALUE)
        CloseHandle(f);

    if (file_data != NULL)
        free(file_data);

    if (label != NULL)
        free(label);

    return NULL;
}

static void close_label(struct label *label)
{
    if (label == NULL)
        return;

    if (label->header != NULL)
        free(label->header);

    free(label);
}

static BOOL print_label(
    HDC printer_context,
    char *printer_name,
    char *filename)
{
    BOOL success = FALSE;
    struct label *label = NULL;

    int page_w, page_h;
    int print_w, print_h;
    int print_resx, print_resy;
    int print_offx, print_offy;

    int bitmap_print_w, bitmap_print_h;
    int bitmap_print_offx, bitmap_print_offy;

    int saved_state = 0;
    DOCINFOA doc_info = {0};

    label = open_label(filename);
    if (label == NULL)
    {
        ERR("Failed to open %s.\n", filename);
        goto exit;
    }

    /* Figure out the printable area in pixels, and the resolution in
     * pixels per meter. */
    page_w = GetDeviceCaps(printer_context, PHYSICALWIDTH);
    page_h = GetDeviceCaps(printer_context, PHYSICALHEIGHT);
    print_w = GetDeviceCaps(printer_context, HORZRES);
    print_h = GetDeviceCaps(printer_context, VERTRES);

    print_resx = GetDeviceCaps(printer_context, LOGPIXELSX) * 10000 / 254;
    print_resy = GetDeviceCaps(printer_context, LOGPIXELSY) * 10000 / 254;

    print_offx = GetDeviceCaps(printer_context, PHYSICALOFFSETX);
    print_offy = GetDeviceCaps(printer_context, PHYSICALOFFSETY);

    DBG("Page size: %d x %d px\n", page_w, page_h);
    DBG("Printable area: %d x %d px\n", print_w, print_h);
    DBG("Printer resolution: %d x %d px/m\n", print_resx, print_resy);
    DBG("Printer offset: %d x %d px\n", print_offx, print_offy);

    /* Convert bitmap into printer units and calculate offset
     * to center on printable area. */
    bitmap_print_w = (label->width * print_resx) / label->xres;
    bitmap_print_h = (label->height * print_resy) / label->yres;
    bitmap_print_offx = (print_w - bitmap_print_w) / 2;
    bitmap_print_offy = (print_h - bitmap_print_h) / 2;

    DBG("Bitmap print size: %d x %d px\n", bitmap_print_w, bitmap_print_h);
    DBG("Bitmap print offset: %d x %d px\n", bitmap_print_offx, bitmap_print_offy);

    /* Before we mess with the printer, we'll store its state. */
    saved_state = SaveDC(printer_context);
    if (saved_state <= 0)
    {
        ERR("Failed to save printer context.\n");
        goto exit;
    }

    /* Set up the coordinate space to handle the scaling. */
    if (SetMapMode(printer_context, MM_ANISOTROPIC) == 0)
    {
        ERR("Failed to set map mode.\n");
        goto exit;
    }

    if (SetWindowExtEx(
            printer_context,
            label->width,
            label->height,
            NULL) == 0)
    {
        ERR("Failed to set window extents.\n");
        goto exit;
    }

    if (SetViewportExtEx(
            printer_context,
            bitmap_print_w,
            bitmap_print_h,
            NULL) == 0)
    {
        ERR("Failed to set viewport extents.\n");
        goto exit;
    }

    if (SetViewportOrgEx(
            printer_context,
            bitmap_print_offx,
            bitmap_print_offy,
            NULL) == 0)
    {
        ERR("Failed to set viewport origin.\n");
        goto exit;
    }

    /* Our print job will be a document with just one page in it. */
    doc_info.cbSize = sizeof(DOCINFOA);
    doc_info.lpszDocName = filename;
    doc_info.lpszOutput = NULL;
    doc_info.lpszDatatype = NULL;
    doc_info.fwType = 0;

    if (dry_run)
    {
        /* Skip the actual print. */
        success = TRUE;
        goto exit;
    }

    if (StartDocA(
            printer_context,
            &doc_info) <= 0)
    {
        ERR("Failed to start document.\n");
        goto exit;
    }

    if (StartPage(printer_context) <= 0)
    {
        ERR("Failed to start page.\n");
        goto exit;
    }

    if (StretchDIBits(
            printer_context,
            0, 0, label->width, label->height,
            0, 0, label->width, label->height,
            label->bits,
            (BITMAPINFO *)label->info_header,
            DIB_RGB_COLORS,
            SRCCOPY) <= 0)
    {
        ERR("Failed to print label.\n");
        goto exit;
    }

    if (EndPage(printer_context) <= 0)
    {
        ERR("Failed to end page.\n");
        goto exit;
    }

    if (EndDoc(printer_context) <= 0)
    {
        ERR("Failed to end document.\n");
        goto exit;
    }

    success = TRUE;

exit:
    if (label != NULL)
    {
        close_label(label);
    }

    if (saved_state > 0 && !RestoreDC(printer_context, saved_state))
    {
        ERR("Failed to restore printer context.\n");
        success = FALSE;
    }

    return success;
}

int main(int argc, char **argv)
{
    char *printer_name = NULL, *default_printer_name = NULL;
    char *paper_size_name = NULL, *default_paper_size_name = NULL;
    char *orientation = NULL;
    BOOL is_landscape = FALSE;
    int file_count = 0;
    int opt;

    SetConsoleOutputCP(CP_UTF8);

    HDC context = NULL;
    DEVMODE *devmode = NULL;
    struct paper_size *paper_size = NULL;

    while ((opt = getopt_long(
                argc,
                argv,
                "p:s:o:dvh",
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

        case 'o':
            orientation = optarg;
            break;

        case 'd':
            dry_run = TRUE;
            break;

        case 'v':
            verbose = TRUE;
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

    file_count = argc - optind;
    if (file_count <= 0)
    {
        ERR("No files to process!\n");
        print_usage();
        exit(EXIT_FAILURE);
    }

    if (orientation != NULL)
    {
        for (int i = 0; i < strlen(orientation); i++)
        {
            orientation[i] = tolower(orientation[i]);
        }
    }
    else
    {
        orientation = "portrait";
    }

    if (strcmp(orientation, "landscape") == 0)
    {
        is_landscape = TRUE;
    }
    else if (strcmp(orientation, "portrait") == 0)
    {
        is_landscape = FALSE;
    }
    else
    {
        ERR("Invalid orientation: %s\n", orientation);
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

    printf(" 🖨️ %s\n", printer_name);
    printf(
        " 📄 %s (%s) %.1f x %.1f mm\n",
        paper_size_name,
        orientation,
        paper_size->width_mm,
        paper_size->height_mm);

    if (dry_run)
    {
        printf(" ⚠️ Dry run only.\n");
    }

    /* Set up the printer context for printing the labels. */
    devmode = set_paper_size(printer_name, paper_size, is_landscape);
    if (devmode == NULL)
    {
        goto exit;
    }

    context = CreateDC("WINSPOOL", printer_name, NULL, devmode);
    if (context == NULL)
    {
        ERR("Failed to create printer context.\n");
        goto exit;
    }

    for (int i = 0; i < file_count; i++)
    {
        char *filename = argv[optind + i];
        if (!print_label(context, printer_name, filename))
        {
            ERR("Failed to print %s.\n", filename);
            goto exit;
        }

        printf(" 🏷️ %s\n", filename);
    }

exit:
    if (default_printer_name != NULL)
        free(default_printer_name);

    if (default_paper_size_name != NULL)
        free(default_paper_size_name);

    if (devmode != NULL)
        free(devmode);

    if (paper_size != NULL)
        free(paper_size);

    if (context != NULL)
        DeleteDC(context);

    return 0;
}
